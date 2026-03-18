/*
 * gensystray.c
 * This file is part of GenSysTray
 * Copyright (C) 2016 - 2026  Darcy Bras da Silva
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* we use gtk for our tray icon system */
#include <gtk/gtk.h>

/* import our configuration routines  */
#include "gensystray_config_parser.h"

/* import config monitor for live config reloading */
#include "gensystray_config_monitor.h"

/* macOS global event monitor for menu dismissal outside GTK windows */
#include "gensystray_osx_menu.h"


/* global list of all loaded instances — used by popdown_all_menus and
 * check_all_exited to coordinate across instances without passing state
 * through signal callbacks
 */
static GSList *all_configs = NULL;

/* dismiss every open instance menu. called when any tray icon is clicked
 * or when a click outside all GTK windows is detected by the osx monitor
 */
static void popdown_all_menus(void) {
	for(GSList *l = all_configs; l; l = l->next) {
		struct config *c = (struct config *)l->data;
		if(c->menu) {
			gtk_menu_popdown(GTK_MENU(c->menu));
			c->menu = NULL;
		}
	}
}

/* called after each instance exits. if no instances have a tray icon
 * remaining, quit the gtk main loop and let the process exit cleanly
 */
static void check_all_exited(void) {
	for(GSList *l = all_configs; l; l = l->next) {
		struct config *c = (struct config *)l->data;
		if(c->tray_icon)
			return;
	}
	gtk_main_quit();
}

/* valid "activate" signal callback. spawns the associated command in a
 * child process via sh -c, keeping the ui responsive
 */
void delegate_system_call(GtkWidget *widget, gpointer user_data) {
	char *argv[] = { "sh", "-c", (char *)user_data, NULL };
	g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
}

/* valid GFunc signature for g_slist_foreach. builds one GtkMenuItem per
 * option and appends it to the menu. separators are detected by the "--"
 * convention on both name and command fields
 */
void gensystray_option_to_item(gpointer data, gpointer param) {
	GtkMenu *menu = (GtkMenu*)param;
	struct option *option = (struct option*)data;
	GtkWidget *menu_item = NULL;

	if('-' == option->name[0] && '-' == option->command[0]) {
		menu_item = gtk_separator_menu_item_new();
	} else {
		menu_item = gtk_menu_item_new_with_label(option->name);
		g_signal_connect(G_OBJECT(menu_item), "activate",
				 G_CALLBACK(delegate_system_call),
				 option->command);
	}

	gtk_menu_shell_append((GtkMenuShell*)menu, menu_item);
	gtk_widget_show_all(GTK_WIDGET(menu));
}

/* hides and releases the tray icon for this instance, then checks if all
 * instances have exited. connected to the "exit" menu item per instance
 */
static void destroy_instance(GtkWidget *widget, gpointer user_data) {
	struct config *config = (struct config *)user_data;
	gtk_status_icon_set_visible(GTK_STATUS_ICON(config->tray_icon), FALSE);
	g_object_unref(config->tray_icon);
	config->tray_icon = NULL;
	config->menu = NULL;
	check_all_exited();
}

/* popup-menu signal callback. dismisses all open menus first (handles the
 * case where another instance's menu is open), then builds and shows a new
 * menu for this instance.
 *
 * the deactivate signal is used to:
 *   - clear config->menu so we don't hold a stale pointer
 *   - uninstall the osx global event monitor
 *
 * the osx monitor is installed after popup so any outside click triggers
 * popdown_all_menus, which also covers clicking between instances
 */
void gensystray_on_menu(GtkStatusIcon *icon, guint button,
			guint activate_time, gpointer user_data) {
	struct config *config = (struct config*)user_data;

	popdown_all_menus();

	GtkMenu *menu = (GtkMenu*)gtk_menu_new();
	config->menu = menu;

	g_slist_foreach(config->options, gensystray_option_to_item, menu);

	GtkWidget *exit_item = gtk_menu_item_new_with_label("exit");
	g_signal_connect(G_OBJECT(exit_item), "activate",
			 G_CALLBACK(destroy_instance), config);
	gtk_menu_shell_append((GtkMenuShell*)menu, exit_item);

	gtk_widget_show_all(GTK_WIDGET(menu));

	g_signal_connect_swapped(G_OBJECT(menu), "deactivate",
				 G_CALLBACK(g_nullify_pointer), &config->menu);
	g_signal_connect_swapped(G_OBJECT(menu), "deactivate",
				 G_CALLBACK(osx_menu_unwatch), menu);

	gtk_menu_popup((GtkMenu*)menu, NULL, NULL,
		       gtk_status_icon_position_menu, icon,
		       button, activate_time);

	osx_menu_watch(menu, popdown_all_menus);
}

/* creates a GtkStatusIcon for the given config instance, sets its icon
 * and tooltip, connects the popup-menu signal, and stores the icon pointer
 * in config->tray_icon to keep it alive
 */
static GtkStatusIcon *init_tray(struct config *config) {
	GtkStatusIcon *icon = NULL;

	if(!config->icon_path) {
		icon = gtk_status_icon_new_from_stock(GTK_STOCK_INFO);
	} else {
		icon = gtk_status_icon_new_from_file(config->icon_path);
	}

	if(config->tooltip)
		gtk_status_icon_set_tooltip_text(icon, config->tooltip);

	g_signal_connect(G_OBJECT(icon), "popup-menu",
			 G_CALLBACK(gensystray_on_menu), config);

	gtk_status_icon_set_visible(icon, TRUE);

	config->tray_icon = icon;
	return icon;
}

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);

	char *cfg_path = get_config_path();
	if(!cfg_path) {
		fprintf(stderr, "couldn't build cfg path\n");
		exit(EXIT_FAILURE);
	}

	all_configs = load_config(cfg_path);
	if(!all_configs) {
		fprintf(stderr, "could not load config file\n");
		free(cfg_path);
		exit(EXIT_FAILURE);
	}

	/* monitor watches the first config's path — all instances share one file */
	GFileMonitor *cfg_monitor = monitor_config(cfg_path, (struct config *)all_configs->data);
	free(cfg_path);

	g_slist_foreach(all_configs, (GFunc)init_tray, NULL);

	gtk_main();

	for(GSList *l = all_configs; l; l = l->next) {
		struct config *c = (struct config *)l->data;
		if(c->tray_icon)
			g_object_unref(c->tray_icon);
	}
	free_configs(all_configs);
	g_object_unref(cfg_monitor);

	return 0;
}
