/*
 * gensystray_config_monitor.h
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

#ifndef _GENSYSTRAY_CFG_MONITOR_H
#define _GENSYSTRAY_CFG_MONITOR_H

#include <gio/gio.h>
#include "gensystray_config_parser.h"

/* sets up a GFileMonitor on config_path and wires on_cfg_changed.
 * config is updated in place when the file changes.
 * returns the GFileMonitor — caller must g_object_unref when done.
 * returns NULL on failure.
 */
GFileMonitor *monitor_config(const char *config_path, struct config *config);

#endif
