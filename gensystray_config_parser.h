/*
 * gensystray_config_parser.h
 * This file is part of GenSysTray
 * Copyright (C) 2016 - 2026  Darcy Bras da Silva
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

#ifndef _GENSYSTRAY_CFG_PARSER_H
#define _GENSYSTRAY_CFG_PARSER_H

#include <stdio.h>
#include <glib.h>

/* our options from the configuration file consist of name
 * and a command associated with that name.
 * we use this struct to keep this proximity relation clear
 * through out the code
 */
struct option {
	char *name;
	char *command;
};

/* this function returns a new string with the path to the configuration
 * file. This string needs to be freed.
 * the string will contain the value of GENSYSTRAY_PATH environment variable
 * if this is set, otherwise it will fallback to unix default
 * $HOME/.config/gensystray/gensystray.cfg
 */
char *get_config_path(void);


/* holds all configuration state loaded from the config file */
struct config {
	char *config_path;
	char *icon_path;
	char *tooltip;
	GSList *options;
};

/* allocates, populates and returns a struct config from the file at config_path.
 * returns NULL on failure. caller must free with free_config.
 */
struct config *load_config(const char *config_path);

/* frees all memory owned by config, including the struct itself */
void free_config(struct config *config);

#endif
