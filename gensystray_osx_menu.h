/*
 * gensystray_osx_menu.h
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

#ifndef GENSYSTRAY_OSX_MENU_H
#define GENSYSTRAY_OSX_MENU_H

#include <gtk/gtk.h>

/* install a global NSEvent monitor that popdowns the given menu
 * when a mouse click occurs outside it. call osx_menu_unwatch
 * from the menu's "deactivate" signal to clean up.
 */
typedef void (*osx_dismiss_fn)(void);

void osx_menu_watch(GtkMenu *menu, osx_dismiss_fn on_outside_click);
void osx_menu_unwatch(GtkMenu *menu);

#endif
