/*
 * gensystray.c
 * This file is part of GenSysTray
 * Copyright (C) 2016 - 2026  Darcy Bras da Silva
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
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

/* import common errors definitions */
#include "gensystray_errors.h"

/* import config monitor for live config reloading */
#include "gensystray_config_monitor.h"


/* this function is a valid signature to the "activate" signal
 * that gtk expects. it spawns the command in a child process
 * using the shell, keeping the ui responsive
 */
void delegate_system_call(GtkWidget *widget, gpointer user_data) {
	char *argv[] = { "sh", "-c", (char *)user_data, NULL };
	g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
}

/* this function is a valid GFunc signature for g_slist_foreach
 * it generates a menu item for each option and appends it to the menu
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

	// associate the button/item to the menu
	gtk_menu_shell_append((GtkMenuShell*)menu, menu_item);

	// make them visible
	gtk_widget_show_all(GTK_WIDGET(menu));
}

/* this functions is creates our popup-menu when it's pressed
 */
void gensystray_on_menu(GtkStatusIcon *icon, guint button,
			guint activate_time, gpointer user_data) {
	GtkMenu *menu = (GtkMenu*)gtk_menu_new();

	struct config *config = (struct config*)user_data;

	// for each option, add a button and create it
	g_slist_foreach(config->options, gensystray_option_to_item, menu);

	// add default exit button
	GtkWidget *exit_item = NULL;
	exit_item = gtk_menu_item_new_with_label("exit");
	g_signal_connect(G_OBJECT(exit_item), "activate",
			 G_CALLBACK(gtk_main_quit),
			 NULL);

	gtk_menu_shell_prepend((GtkMenuShell*)menu, exit_item);
	gtk_widget_show_all(GTK_WIDGET(menu));
	
	// show the menu
	gtk_menu_popup((GtkMenu*)menu, NULL, NULL, NULL, NULL,
		       button, activate_time);
}


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

	return icon;
}

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);

	char *cfg_path = get_config_path();
	if(!cfg_path) {
		fprintf(stderr, "couldn't build cfg path\n");
		exit(MEMORY_ERROR);
	}

	struct config *config = load_config(cfg_path);
	if(!config) {
		fprintf(stderr, "could not load config file\n");
		free(cfg_path);
		exit(CFG_NOT_FOUND);
	}

	GFileMonitor *cfg_monitor = monitor_config(cfg_path, config);
	free(cfg_path);

	init_tray(config);

	gtk_main();

	free_config(config);
	g_object_unref(cfg_monitor);

	return 0;
}
