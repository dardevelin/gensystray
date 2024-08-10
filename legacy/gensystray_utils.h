/*
 * gensystray_utils.h
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

#ifndef _GENSYSTRAY_UTILS_H
#define _GENSYSTRAY_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gensystray_errors.h"

/* returns NULL if src is null
 * returns NULL if slen is 0
 * returns NULL if couldn't allocate memory
 * copies 'slen' from src into a new string which needs to be freed
 * notice: slen+terminator is the actual allocated size
 */
char *sstrndup(const char *src, size_t slen);

/* this function finds 'c' in stream starting from 'pos'.
 * pos - is the value of current stream position as by ftell
 * returns the result of ftell-1 once it's found so that 'c'
 * can be read via fgetc
 *
 * if 'c' is not found returns -1
 * if EOF is found returns -1 as it's the same as not found
 *
 * notice: this function changes the stream position
 */
long fstrchr(FILE * const stream, const long pos, const int c);

/* this function returns a new null terminated string with the contents
 * found between 'start_pos' and 'end_pos' stream positions
 * returns NULL when it fails to allocate memory
 * FIXME: make it return NULL when errno is set by fseek
 */
char *fextract(FILE *stream, const long start_pos, const long end_pos);

#endif
