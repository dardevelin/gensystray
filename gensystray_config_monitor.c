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

#include "gensystray_config_monitor.h"

GFileMonitor *monitor_config(const char *config_path,
			     config_changed_fn on_changed,
			     gpointer user_data) {
	if(!config_path || !on_changed)
		return NULL;

	GFile *cfg_gfile = g_file_new_for_path(config_path);
	GFileMonitor *monitor = g_file_monitor_file(cfg_gfile, G_FILE_MONITOR_NONE, NULL, NULL);
	g_object_unref(cfg_gfile);

	if(!monitor)
		return NULL;

	g_signal_connect(G_OBJECT(monitor), "changed",
			 G_CALLBACK(on_changed), user_data);

	return monitor;
}
