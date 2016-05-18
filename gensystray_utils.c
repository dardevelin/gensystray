/*
 * gensystray_utils.c
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

#include "gensystray_utils.h"

char *sstrndup(const char *src, size_t slen)
{
	if(!src || 0==slen) {
		return NULL;
	}

	char *new;
	if( NULL == (new = malloc(slen+1))) {
		fprintf(stderr,"couldn't allocate memory for new string\n");
		return NULL;
	}

	memcpy(new, src, slen);
	new[slen]='\0';
	return new;
}

long fstrchr(FILE * const stream, const long pos, const int c)
{
	fseek(stream, pos, SEEK_SET);
	int ch = 0;
	while(EOF != (ch = fgetc(stream))) {
		if(c == ch ) {
			// rewind the character we just read
			ungetc(ch, stream);
			return ftell(stream);
		}
	}

	return NOT_FOUND;
}

char *fextract(FILE * const stream, const long start_pos, const long end_pos)
{
	fseek(stream, start_pos, SEEK_SET);
	long buf_len = end_pos - start_pos;

	if(0 >= buf_len) {
		fprintf(stderr,"invalid extraction range\n");
		return NULL;
	}

	char *buf = NULL;
	if(NULL == (buf = malloc(buf_len+sizeof('\0')))) {
		fprintf(stderr,"couldn't allocate memory for buffer\n");
		return NULL;
	}

	char *iter;

	//set the terminator ahead of time
	buf[buf_len] = '\0';

	for(iter=buf; buf_len--; ++iter) {
		*iter=fgetc(stream);
	}

	return buf;
}
