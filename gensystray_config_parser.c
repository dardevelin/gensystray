/*
 * gensystray_config_parser.c
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gensystray_config_parser.h"
#include "gensystray_utils.h"
#include "gensystray_errors.h"

// global default values
static const char *def_config_path = ".config/gensystray";
static const char *def_config_file = "gensystray.cfg";

char *get_config_path(void)
{
	char *gensystray_env = NULL;
	gensystray_env = getenv("GENSYSTRAY_PATH");
	if(NULL != gensystray_env) {
		return sstrndup(gensystray_env, strlen(gensystray_env));
	}

	// environment variable is not set, lets find HOME
	const char *home = NULL;
	home = getenv("HOME");
	if(NULL == home) {
		fprintf(stderr,"couldn't find home directory\n");
		exit(NO_PRE_REQ);
	}

	// calculate the size of the path
	size_t path_buf_len = strlen(home);
	path_buf_len += sizeof('/');
	path_buf_len += strlen(def_config_path);
	path_buf_len += sizeof('/');
	path_buf_len += strlen(def_config_file);

	char *path = NULL;
	if(NULL == (path = malloc(path_buf_len+sizeof('\0')))) {
		fprintf(stderr,"couldn't allocate memory for config_path\n");
		return NULL;
	}

	sprintf(path,"%s/%s/%s", home, def_config_path, def_config_file);
	return path;
}

struct sOption *get_config_option(FILE *stream)
{
	//find option
	long optstart = 0;

	optstart = fstrchr(stream, ftell(stream), '[');

	if(OPT_NOT_FOUND == optstart) {
		return NULL;
	}

	long optend = 0;

	optend = fstrchr(stream, ftell(stream), ']');

	if(NOT_FOUND == optend) {
		return NULL;
	}

	// skipping the '[' character
	char *name = fextract(stream, optstart+1, optend);

	if(!name) {
		fprintf(stderr,"fextract failed to give a name\n");
		return NULL;
	}

	// this is the new line after the name
	optstart = fstrchr(stream, ftell(stream), '\n');
	// skip the new line
	int ch = fgetc(stream);

	if(EOF == ch) {
		fprintf(stderr,"formating error, couldn't find command\n");
		exit(FMT_CFG_ERROR);
	}

	if(NOT_FOUND == optstart) {
		fprintf(stderr,"formating error, couldn't find command\n");
		free(name);
		//FIXME: change to graceful degradation to be handle by main
		exit(FMT_CFG_ERROR);
	}

	optend = fstrchr(stream, ftell(stream), '\n');

	if(NOT_FOUND == optend) {
		fprintf(stderr,"formanting error, couln't find command\n");
		exit(FMT_CFG_ERROR);
	}

	// +1 don't include the '\n' character in the command
	char *command = fextract(stream, optstart+1, optend);

	if(!command) {
		fprintf(stderr,"fextract failed to give a command\n");
		free(name);
		return NULL;
	}

	struct sOption *option = malloc(sizeof(struct sOption));

	if(!option) {
		fprintf(stderr,"couldn't allocate memory for option structure\n");
		free(name);
		free(command);
		return NULL;
	}

	option->name = name;
	option->command = command;

	return option;
}

char *get_icon_path(FILE *stream)
{
	char *ipath = NULL;
	const long stream_pos = ftell(stream);

	rewind(stream);

	long start_pos = fstrchr(stream, ftell(stream), '@');

	if(NOT_FOUND == start_pos) {
		fprintf(stderr,"couldn't find icon path in file\n");
		goto clean_exit;
	}

	long end_pos = fstrchr(stream, ftell(stream), '\n');

	if(NOT_FOUND == end_pos) {
		fprintf(stderr,"formating error, icon path invalid\n");
		goto clean_exit;
	}

	// +1 to skip the '@'
	ipath = fextract(stream, start_pos+1, end_pos);

clean_exit:
	fseek(stream, stream_pos, SEEK_SET);
	return ipath;
}
