/*
 * gensystray_platform.h
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

#ifndef GENSYSTRAY_PLATFORM_H
#define GENSYSTRAY_PLATFORM_H

#include <gtk/gtk.h>

/* callback type for platform menu dismiss handlers */
typedef void (*platform_dismiss_fn)(void);

/* install a platform-specific monitor that calls on_outside_click when
 * the user clicks outside the given menu.  on macOS this uses an NSEvent
 * global monitor; on Linux this is a no-op (GTK handles it natively).
 *
 * call platform_menu_unwatch from the menu's "deactivate" signal.
 */
void platform_menu_watch(GtkMenu *menu, platform_dismiss_fn on_outside_click);
void platform_menu_unwatch(GtkMenu *menu);

#endif
