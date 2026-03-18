/*
 * gensystray_config_parser.c
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <ucl.h>

#include "gensystray_config_parser.h"

// global default values
static const char *def_config_path = ".config/gensystray";
static const char *def_config_file = "gensystray.cfg";

char *get_config_path(void) {
	char *gensystray_env = getenv("GENSYSTRAY_PATH");
	if(NULL != gensystray_env) {
		return strdup(gensystray_env);
	}

	const char *home = getenv("HOME");
	if(NULL == home) {
		fprintf(stderr, "couldn't find home directory\n");
		return NULL;
	}

	size_t len = strlen(home) + 1
	           + strlen(def_config_path) + 1
	           + strlen(def_config_file) + 1;

	char *path = malloc(len);
	if(NULL == path) {
		fprintf(stderr, "couldn't allocate memory for config_path\n");
		return NULL;
	}

	snprintf(path, len, "%s/%s/%s", home, def_config_path, def_config_file);
	return path;
}

static int option_order_cmp(const void *a, const void *b) {
	return ((const struct option *)a)->order - ((const struct option *)b)->order;
}

static void option_dalloc(void *data) {
	struct option *opt = (struct option *)data;
	free(opt->name);
	free(opt->command);
	free(opt);
}

// parse items from a UCL object scope (top-level or instance block)
// items with order come first (sorted), then declaration-order items after
static GSList *parse_items(const ucl_object_t *scope) {
	GSList *ordered = NULL;
	GSList *unordered = NULL;

	ucl_object_iter_t it = ucl_object_iterate_new(scope);
	const ucl_object_t *cur;

	for(; NULL != (cur = ucl_object_iterate_safe(it, true)); ) {
		if(strcmp(ucl_object_key(cur), "item") != 0)
			continue;

		// two-level iteration: expand=false to get each array element,
		// then expand=true on each element to get the named block key
		ucl_object_iter_t iit = NULL;
		const ucl_object_t *elem;
		for(; NULL != (elem = ucl_iterate_object(cur, &iit, false)); ) {
		ucl_object_iter_t iiit = NULL;
		const ucl_object_t *item;
		for(; NULL != (item = ucl_iterate_object(elem, &iiit, true)); ) {
			struct option *opt = malloc(sizeof(struct option));
			if(!opt) {
				fprintf(stderr, "couldn't allocate option\n");
				continue;
			}

			// block label: item "New Button 1" { } — key is "New Button 1"
			const char *key = ucl_object_key(item);
			opt->name = strdup(key ? key : "");

			// separator
			const ucl_object_t *sep = ucl_object_lookup(item, "separator");
			if(sep && ucl_object_toboolean(sep)) {
				free(opt->name);
				opt->name    = strdup("--");
				opt->command = strdup("--");
			} else {
				const ucl_object_t *cmd = ucl_object_lookup(item, "command");
				if(cmd) {
					opt->command = strdup(ucl_object_tostring(cmd));
				} else {
					opt->command = strdup("");
				}
			}

			// ordering
			const ucl_object_t *ord = ucl_object_lookup(item, "order");
			if(ord) {
				opt->order = (int)ucl_object_toint(ord);
				ordered = g_slist_prepend(ordered, opt);
			} else {
				opt->order = -1;
				unordered = g_slist_prepend(unordered, opt);
			}

		}
		}
	}

	ucl_object_iterate_free(it);

	// sort ordered items by order value
	ordered = g_slist_sort(ordered, (GCompareFunc)option_order_cmp);

	// ordered first, then unordered in declaration order
	unordered = g_slist_reverse(unordered);
	return g_slist_concat(ordered, unordered);
}

static struct config *parse_scope(const char *config_path, const ucl_object_t *scope) {
	struct config *config = malloc(sizeof(struct config));
	if(!config) {
		fprintf(stderr, "load_config: couldn't allocate config\n");
		return NULL;
	}

	config->config_path = strdup(config_path);
	config->icon_path   = NULL;
	config->tooltip     = NULL;
	config->options     = NULL;
	config->tray_icon   = NULL;
	config->menu        = NULL;

	const ucl_object_t *tray = ucl_object_lookup(scope, "tray");
	if(tray) {
		const ucl_object_t *icon = ucl_object_lookup(tray, "icon");
		if(icon)
			config->icon_path = strdup(ucl_object_tostring(icon));

		const ucl_object_t *tooltip = ucl_object_lookup(tray, "tooltip");
		if(tooltip)
			config->tooltip = strdup(ucl_object_tostring(tooltip));
	}

	config->options = parse_items(scope);

	return config;
}

GSList *load_config(const char *config_path) {
	if(!config_path) {
		fprintf(stderr, "load_config: config_path is NULL\n");
		return NULL;
	}

	struct ucl_parser *parser = ucl_parser_new(0);
	if(!parser) {
		fprintf(stderr, "load_config: couldn't create UCL parser\n");
		return NULL;
	}

	if(!ucl_parser_add_file(parser, config_path)) {
		fprintf(stderr, "load_config: %s\n", ucl_parser_get_error(parser));
		ucl_parser_free(parser);
		return NULL;
	}

	const ucl_object_t *root = ucl_parser_get_object(parser);
	if(!root) {
		fprintf(stderr, "load_config: empty config\n");
		ucl_parser_free(parser);
		return NULL;
	}

	GSList *configs = NULL;

	// single instance: top-level tray block present
	if(ucl_object_lookup(root, "tray")) {
		struct config *config = parse_scope(config_path, root);
		if(config)
			configs = g_slist_append(configs, config);
	} else {
		// multi instance: iterate instance blocks
		ucl_object_iter_t it = ucl_object_iterate_new(root);
		const ucl_object_t *cur;
		for(; NULL != (cur = ucl_object_iterate_safe(it, true)); ) {
			if(strcmp(ucl_object_key(cur), "instance") != 0)
				continue;
			ucl_object_iter_t iit = NULL;
			const ucl_object_t *elem;
			for(; NULL != (elem = ucl_iterate_object(cur, &iit, false)); ) {
				ucl_object_iter_t iiit = NULL;
				const ucl_object_t *inst;
				for(; NULL != (inst = ucl_iterate_object(elem, &iiit, true)); ) {
					struct config *config = parse_scope(config_path, inst);
					if(config)
						configs = g_slist_append(configs, config);
				}
			}
		}
		ucl_object_iterate_free(it);
	}

	ucl_object_unref((ucl_object_t *)root);
	ucl_parser_free(parser);

	return configs;
}

void free_configs(GSList *configs) {
	g_slist_free_full(configs, (GDestroyNotify)free_config);
}

void free_config(struct config *config) {
	if(!config)
		return;

	free(config->config_path);
	free(config->icon_path);
	free(config->tooltip);

	g_slist_free_full(config->options, option_dalloc);

	free(config);
}
