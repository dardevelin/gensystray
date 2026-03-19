/*
 * gensystray_config_monitor.h
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

#ifndef _GENSYSTRAY_CFG_MONITOR_H
#define _GENSYSTRAY_CFG_MONITOR_H

#include <gio/gio.h>

/* callback type for config file change notification */
typedef void (*config_changed_fn)(GFileMonitor *monitor, GFile *file,
				  GFile *other_file,
				  GFileMonitorEvent event_type,
				  gpointer user_data);

/* sets up a GFileMonitor on config_path and calls on_changed when the
 * file changes. the caller defines the reload policy via on_changed.
 * returns the GFileMonitor — caller must g_object_unref when done.
 * returns NULL on failure.
 */
GFileMonitor *monitor_config(const char *config_path,
			     config_changed_fn on_changed,
			     gpointer user_data);

#endif
