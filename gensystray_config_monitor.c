/*
 * gensystray_config_monitor.c
 * This file is part of GenSysTray
 * Copyright (C) 2026  Darcy Bras da Silva
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
#include <stdlib.h>

#include "gensystray_config_monitor.h"

static void on_cfg_changed(GFileMonitor *monitor, GFile *file, GFile *other_file,
			   GFileMonitorEvent event_type, gpointer user_data) {
	struct config *config = (struct config *)user_data;

	GSList *updated_list = load_config(config->config_path);
	if(!updated_list) {
		fprintf(stderr, "on_cfg_changed: failed to reload config\n");
		return;
	}
	struct config *updated = (struct config *)updated_list->data;

	// swap the reloadable fields, free the old ones via free_config trick
	GSList *old_options   = config->options;
	char   *old_icon_path = config->icon_path;
	char   *old_tooltip   = config->tooltip;

	config->options   = updated->options;
	config->icon_path = updated->icon_path;
	config->tooltip   = updated->tooltip;

	// point updated at old data so free_config cleans it up
	updated->options   = old_options;
	updated->icon_path = old_icon_path;
	updated->tooltip   = old_tooltip;

	free_configs(updated_list);
}

GFileMonitor *monitor_config(const char *config_path, struct config *config) {
	if(!config_path || !config)
		return NULL;

	GFile *cfg_gfile = g_file_new_for_path(config_path);
	GFileMonitor *monitor = g_file_monitor_file(cfg_gfile, G_FILE_MONITOR_NONE, NULL, NULL);
	g_object_unref(cfg_gfile);

	if(!monitor)
		return NULL;

	g_signal_connect(G_OBJECT(monitor), "changed",
			 G_CALLBACK(on_cfg_changed), config);

	return monitor;
}
