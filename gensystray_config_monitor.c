/*
 * gensystray_config_monitor.c
 * This file is part of GenSysTray
 * Copyright (C) 2026  Darcy Bras da Silva
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
#include <stdlib.h>

#include "gensystray_config_monitor.h"
#include "gensystray_config_parser.h"

static void option_dalloc(void *data)
{
	struct sOption *opt = (struct sOption *)data;
	free(opt->name);
	free(opt->command);
	free(opt);
}

static void on_cfg_changed(GFileMonitor *monitor, GFile *file, GFile *other_file,
			   GFileMonitorEvent event_type, gpointer user_data)
{
	GSList **optlist = (GSList **)user_data;
	struct sOption *option = NULL;
	char *cfg_path = NULL;
	FILE *cfg = NULL;

	cfg_path = get_config_path();
	if(!cfg_path) {
		fprintf(stderr, "couldn't handle build cfg_path on changes\n");
		return;
	}

	if(NULL == (cfg = fopen(cfg_path, "r"))) {
		fprintf(stderr, "could not load config file on change\n");
		free(cfg_path);
		return;
	}

	free(cfg_path);

	g_slist_free_full(*optlist, option_dalloc);
	*optlist = NULL;

	while(NULL != (option = get_config_option(cfg))) {
		*optlist = g_slist_prepend(*optlist, option);
	}

	*optlist = g_slist_reverse(*optlist);

	fclose(cfg);
}

GFileMonitor *monitor_config(const char *config_path, GSList **optlist)
{
	if(!config_path || !optlist)
		return NULL;

	GFile *cfg_gfile = g_file_new_for_path(config_path);
	GFileMonitor *monitor = g_file_monitor_file(cfg_gfile, G_FILE_MONITOR_NONE, NULL, NULL);
	g_object_unref(cfg_gfile);

	if(!monitor)
		return NULL;

	g_signal_connect(G_OBJECT(monitor), "changed",
			 G_CALLBACK(on_cfg_changed), optlist);

	return monitor;
}
