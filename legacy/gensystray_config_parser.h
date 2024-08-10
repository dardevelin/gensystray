/*
 * gensystray_config_parser.h
 * This file is part of GenSysTray
 * Copyright (C) 2016  Darcy Bras da Silva
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

/* our options from the configuration file consist of name
 * and a command associated with that name.
 * we use this struct to keep this proximity relation clear
 * through out the code
 */
struct sOption {
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

/* this function takes a FILE pointer to stream which is expected
 * to be open, and uses it's current position to obtain options from.
 * It returns options until it reaches EOF if called consecutively.
 *
 * returns NULL if no option is found
 * returns NULL if no EOF is reached
 *
 * this function changes the position in the stream and does not rewind it.
 * the return struct sOption needs to be freed along with it's data members
 */
struct sOption *get_config_option(FILE *stream);

/* this function takes a FILE pointer to stream which is expected
 * to be open. saves it's current position, rewinds it and searches
 * for the icon_path identifier.
 *
 * returns the path in in a new null terminated string in case of success
 * returns NULL if can't find identifier
 * returns NULL if there is a formating error
 * re-sets the stream back to its original position before returning
 */
char *get_icon_path(FILE *stream);

/* this function takes a FILE pointer to stream which is expected
 * to be open. saves it's current position, rewinds it and searches
 * for tooltip_text identifier.
 *
 * returns the tooltip_tex in a null terminated string in case of success
 * returns NULL if can't find identifier
 * returns NULL if there is a formatting error
 * re-sets the stream back to it's original position before returning
 */
char *get_tooltip_text(FILE *cfg);

#endif
