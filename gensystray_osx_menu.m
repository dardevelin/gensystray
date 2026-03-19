/*
 * gensystray_osx_menu.m
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

#import <Cocoa/Cocoa.h>
#include "gensystray_platform.h"

static id event_monitor = nil;

void platform_menu_watch(GtkMenu *menu, platform_dismiss_fn on_outside_click) {
	if(event_monitor)
		return;

	event_monitor = [NSEvent addGlobalMonitorForEventsMatchingMask:
		(NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown)
		handler:^(NSEvent *event) {
			if(on_outside_click)
				on_outside_click();
		}];
}

void platform_menu_unwatch(GtkMenu *menu) {
	if(!event_monitor)
		return;
	[NSEvent removeMonitor:event_monitor];
	event_monitor = nil;
}
