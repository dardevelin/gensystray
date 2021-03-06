/*
 * gensystray.c
 * This file is part of GenSysTray
 * Copyright (C) 2016  Darcy Bras da Silva
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

/* easy to use portable threads */
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

/* import our doubly linked data structure */
#include "dlist.h"

/* import our configuration routines  */
#include "gensystray_config_parser.h"

/* import common errors definitions */
#include "gensystray_errors.h"

/* import gio to enable easy file changes monitoring */
#include <gio/gio.h>

/* this function allows easy management of optlist clean up
 * by acting as valid signature to dlist_node_foreach
 */
void option_dalloc(void *data)
{
	free(((struct sOption*)data)->name);
	free(((struct sOption*)data)->command);
	free(data);
}

/* this function is an SDL_Thread task so that we can
 * se the execution of the menu_item in separation with
 * the rest of the program execution
 */
int sdl_task(void *data)
{
	system(data);

	return 0;
}

/* this function is a valid signature to the "activate" signal 
 * that gtk expects. it is intended to delegate to a SDL_thread
 * which will execute on it's own the command
 */
void delegate_system_call(GtkWidget *widget, gpointer user_data)
{
	SDL_Thread *thread;
	thread = SDL_CreateThread(sdl_task, "sdl_task", user_data);
	SDL_DetachThread(thread);
}

/* this functions is a valid signature to dlist_node_foreach
 * it is intended to assist gensystray_on_menu by generating
 * a button for each option in optlist/user_data and associating
 * the respective signals to the buttons as well as the buttons
 * to the menu itself
 */
void *gensystray_option_to_item(void *carry, void *data, void *param)
{
	GtkMenu *menu = (GtkMenu*)param;
	struct sOption *option = (struct sOption*)data;
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
	return NULL;
}

/* this functions is creates our popup-menu when it's pressed
 */
void gensystray_on_menu(GtkStatusIcon *icon, guint button,
			guint activate_time, gpointer user_data)
{
	GtkMenu *menu = (GtkMenu*)gtk_menu_new();
	
	struct dlist_list *optlist = (struct dlist_list*)user_data;

	// for each option, add a button and create it
	dlist_node_foreach(optlist, gensystray_option_to_item, menu);

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

void on_cfg_changed(GFileMonitor *monitor, GFile *file, GFile *other_file,
		    GFileMonitorEvent event_type, gpointer user_data)
{	
	struct dlist_list *optlist = (struct dlist_list*)user_data;
	struct sOption *option = NULL;
	char *cfg_path = NULL;
	FILE *cfg = NULL;

	cfg_path = get_config_path();
	if(!cfg_path) {
		fprintf(stderr,"couldn't handle build cfg_path on changes\n");
		return;
	}

	if( NULL == (cfg = fopen(cfg_path, "r")) ) {
		//FIXME: use errno
		//FIXME: create config file
		fprintf(stderr,"coul not load config file on change\n");
		free(cfg_path);
		return;
	}

	// we no longer need the path
	free(cfg_path);

	dlist_list_delete_all_nodes(optlist);

	while(NULL != (option = get_config_option(cfg)) ) {
		dlist_node_push(optlist,
				dlist_node_new(optlist,
					       option,
					       option_dalloc));
	}

	// we no longer need to cfg file
	fclose(cfg);
}

int main(int argc, char **argv)
{
	// we need to init SDL in order to use thread subsystem
	SDL_Init(0);
	// init gtk in order to use the tray icon system
	gtk_init(&argc, &argv);

	// we use a dlist to store our list of options to be presented
	// in our tray icon popup-menu

	struct dlist_list optlist;
	struct sOption *option;
	// we take the options from our configuration file
	FILE *cfg = NULL;
	char *cfg_path = get_config_path();
	char *icon_path = NULL;
	char *tooltip_text = "GenSysTray";
	GtkStatusIcon *icon = NULL;
	GFileMonitor *cfg_monitor = NULL;
	GFile *cfg_gfile = NULL;
	
	// prepare list so it can be used
	dlist_init(&optlist, NULL, NULL);

	if(!cfg_path) {
		fprintf(stderr,"couldn't build cfg path\n");
		exit(MEMORY_ERROR);
	}
	// load_cfg
	if( NULL ==  (cfg = fopen(cfg_path, "r")) ) {
		//FIXME: use errno
		//FIXME: create config file
		fprintf(stderr, "could not load config file\n");
		free(cfg_path);
		exit(CFG_NOT_FOUND);
	}

	// we need cfg as gio to be able to monitor it
	cfg_gfile = g_file_new_for_path(cfg_path);

	// lets monitor the cfg to get live changes
	cfg_monitor = g_file_monitor_file(cfg_gfile, G_FILE_MONITOR_NONE, NULL, NULL);

	// set our handler for changes
	g_signal_connect(G_OBJECT(cfg_monitor), "changed",
			 G_CALLBACK(on_cfg_changed),
			 &optlist);

	// clean up, we no longer need cfg_path
	free(cfg_path);

	if(NULL == (icon_path = get_icon_path(cfg)) ) {
		icon = gtk_status_icon_new_from_stock(GTK_STOCK_INFO);
	} else {
		icon = gtk_status_icon_new_from_file(icon_path);
	}

	// we no longer need icon_path clean it
	free(icon_path);
	icon_path = NULL;

	// get option list
	while(NULL != (option = get_config_option(cfg)) ) {
		dlist_node_push(&optlist,
				dlist_node_new(&optlist,
					       option,
					       option_dalloc));
	}

	g_signal_connect(G_OBJECT(icon), "popup-menu",
			 G_CALLBACK(gensystray_on_menu),
			 &optlist);

	if(NULL != (tooltip_text = get_tooltip_text(cfg)) ) {
		gtk_status_icon_set_tooltip_text(icon, get_tooltip_text(cfg));
		// we no longer need tooltip_text
		free(tooltip_text);
	}
	
	// we no longer need cfg, close it
	fclose(cfg);

	gtk_status_icon_set_visible(icon, TRUE);

	// execute our GenSysTray
	gtk_main();

	// the application is shutting down. clean up
	dlist_list_delete_all_nodes(&optlist);
	// clean our cfg_gfile and cfg_monitor
	g_object_unref(cfg_gfile);
	g_object_unref(cfg_monitor);

	SDL_Quit();
	

	return 0;
}
