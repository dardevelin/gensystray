/*
 * gensystray_linux_menu.c
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

/* Linux/X11/Wayland: GTK handles menu dismissal natively so these are
 * no-ops.  The matching macOS implementation uses NSEvent monitors.
 */

#include "gensystray_platform.h"

void platform_menu_watch(GtkMenu *menu, platform_dismiss_fn on_outside_click) {
	(void)menu;
	(void)on_outside_click;
}

void platform_menu_unwatch(GtkMenu *menu) {
	(void)menu;
}
