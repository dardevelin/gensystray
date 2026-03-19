/*
 * gensystray_config_parser.c
 * This file is part of GenSysTray
 * Copyright (C) 2016 - 2026  Darcy Bras da Silva
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include <ucl.h>
#include <gio/gio.h>

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

/* parse a duration string like "2s", "500ms", "1m", "1h" into milliseconds.
 * returns 0 and logs an error if the format is unrecognised.
 */
static guint parse_duration_ms(const char *s) {
	if(!s || '\0' == s[0])
		return 0;

	char *end = NULL;
	long val  = strtol(s, &end, 10);

	if(end == s || val <= 0) {
		fprintf(stderr, "refresh: invalid value '%s'\n", s);
		return 0;
	}

	if(0 == strcmp(end, "ms")) return (guint)val;
	if(0 == strcmp(end, "s"))  return (guint)(val * 1000);
	if(0 == strcmp(end, "m"))  return (guint)(val * 60 * 1000);
	if(0 == strcmp(end, "h"))  return (guint)(val * 3600 * 1000);

	fprintf(stderr, "refresh: unknown unit in '%s' (use ms, s, m, h)\n", s);
	return 0;
}

static int option_order_cmp(gconstpointer a, gconstpointer b) {
	return ((const struct option *)a)->order - ((const struct option *)b)->order;
}

static int section_order_cmp(gconstpointer a, gconstpointer b) {
	return ((const struct section *)a)->order - ((const struct section *)b)->order;
}

static int option_alpha_cmp(gconstpointer a, gconstpointer b) {
	return strcmp(((const struct option *)a)->name, ((const struct option *)b)->name);
}

static void option_dalloc(void *data) {
	struct option *opt = (struct option *)data;
	if(0 != opt->timer_id)
		g_source_remove(opt->timer_id);
	free(opt->name);
	free(opt->command);
	free(opt->live_cmd);
	free(opt->live_output);
	free(opt);
}

static void populate_dalloc(void *data) {
	struct populate *pop = (struct populate *)data;
	free(pop->from);
	free(pop->pattern);
	free(pop->label_tpl);
	free(pop->command_tpl);
	free(pop);
}

static void section_dalloc(void *data) {
	struct section *sec = (struct section *)data;
	free(sec->label);
	g_slist_free_full(sec->options, option_dalloc);
	g_slist_free_full(sec->populates, populate_dalloc);
	g_slist_free_full(sec->monitors, (GDestroyNotify)g_object_unref);
	free(sec);
}

/* parse item blocks from a UCL scope into a GSList of struct option.
 * named sections sort alphabetically; anonymous sections use declaration order.
 * across sections: explicit order first, then declaration order.
 */
static GSList *parse_options(const ucl_object_t *scope, int named) {
	GSList *ordered = NULL;
	GSList *unordered = NULL;

	ucl_object_iter_t it = ucl_object_iterate_new(scope);
	const ucl_object_t *cur;

	for(; NULL != (cur = ucl_object_iterate_safe(it, true)); ) {
		if(strcmp(ucl_object_key(cur), "item") != 0)
			continue;

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

				const char *key = ucl_object_key(item);
				opt->name = strdup(key ? key : "");

				const ucl_object_t *sep = ucl_object_lookup(item, "separator");
				if(sep && ucl_object_toboolean(sep)) {
					free(opt->name);
					opt->name    = strdup("--");
					opt->command = strdup("--");
					opt->order   = -1;
					unordered = g_slist_prepend(unordered, opt);
					continue;
				}

				const ucl_object_t *cmd = ucl_object_lookup(item, "command");
				opt->command = strdup(cmd ? ucl_object_tostring(cmd) : "");

				/* live item fields */
				opt->live_cmd    = NULL;
				opt->refresh_ms  = 0;
				opt->live_output = NULL;
				opt->timer_id    = 0;
				opt->live_label  = NULL;

				const ucl_object_t *live = ucl_object_lookup(item, "live");
				if(live) {
					const ucl_object_t *ref = ucl_object_lookup(item, "refresh");
					if(!ref) {
						fprintf(stderr, "[!] item '%s': live requires refresh\n", opt->name);
						free(opt->name);
						opt->name    = strdup("[!] missing refresh");
						opt->command = strdup("");
						opt->order   = -1;
						unordered = g_slist_prepend(unordered, opt);
						continue;
					}
					opt->live_cmd    = strdup(ucl_object_tostring(live));
					opt->refresh_ms  = parse_duration_ms(ucl_object_tostring(ref));
					opt->tick_counter = 0;

					const ucl_object_t *ind = ucl_object_lookup(item, "independent");
					opt->independent = ind && ucl_object_toboolean(ind);
				}

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

	ordered = g_slist_sort(ordered, option_order_cmp);

	if(named)
		unordered = g_slist_sort(unordered, option_alpha_cmp);
	else
		unordered = g_slist_reverse(unordered);

	return g_slist_concat(ordered, unordered);
}

/* substitute {filename} and {filepath} tokens in tpl, returns a g_malloc'd string */
static char *apply_tpl(const char *tpl, const char *filename, const char *filepath)
{
	char *result = g_strdup(tpl);

	if(strstr(result, "{filename}")) {
		char **parts = g_strsplit(result, "{filename}", -1);
		g_free(result);
		result = g_strjoinv(filename, parts);
		g_strfreev(parts);
	}

	if(strstr(result, "{filepath}")) {
		char **parts = g_strsplit(result, "{filepath}", -1);
		g_free(result);
		result = g_strjoinv(filepath, parts);
		g_strfreev(parts);
	}

	return result;
}

/* recursively expand dir_path, matching filenames against pat, up to max_depth.
 * depth_left == -1 means unlimited. appends matched options to *opts.
 * if hierarchy is true, subdirectory entries are skipped (caller handles them).
 */
static void expand_dir(const char *dir_path, const char *pat,
		       const struct populate *pop, int depth_left,
		       GSList **opts)
{
	GError *err = NULL;
	GDir   *dir = g_dir_open(dir_path, 0, &err);

	if(!dir) {
		struct option *e = malloc(sizeof(struct option));
		if(e) {
			char buf[512];
			snprintf(buf, sizeof(buf), "[!] watch error: %s",
				 err ? err->message : dir_path);
			e->name    = strdup(buf);
			e->command = strdup("");
			e->order   = -1;
			*opts = g_slist_prepend(*opts, e);
		}
		if(err) g_error_free(err);
		return;
	}

	const char *fname;
	for(; NULL != (fname = g_dir_read_name(dir)); ) {
		char *filepath = g_strconcat(dir_path, G_DIR_SEPARATOR_S, fname, NULL);
		bool  is_dir   = g_file_test(filepath, G_FILE_TEST_IS_DIR);

		if(is_dir) {
			bool can_recurse = depth_left != 0;
			if(can_recurse)
				expand_dir(filepath, pat, pop,
					   depth_left > 0 ? depth_left - 1 : -1,
					   opts);
			g_free(filepath);
			continue;
		}

		if(!g_pattern_match_simple(pat, fname)) {
			g_free(filepath);
			continue;
		}

		char *label = apply_tpl(pop->label_tpl, fname, filepath);
		char *cmd   = apply_tpl(pop->command_tpl, fname, filepath);

		struct option *opt = malloc(sizeof(struct option));
		if(opt) {
			opt->name    = strdup(label);
			opt->command = strdup(cmd);
			opt->order   = -1;
			*opts = g_slist_prepend(*opts, opt);
		}

		g_free(label);
		g_free(cmd);
		g_free(filepath);
	}

	g_dir_close(dir);
}

/* expand a glob pattern into a GSList of struct option using the populate
 * template. ~ is expanded via g_get_home_dir(). uses GDir for cross-platform
 * UTF-8 safe directory iteration. depth and hierarchy are taken from pop.
 */
static GSList *expand_glob(const struct populate *pop)
{
	char *expanded = NULL;

	if('~' == pop->pattern[0])
		expanded = g_strconcat(g_get_home_dir(), pop->pattern + 1, NULL);
	else
		expanded = g_strdup(pop->pattern);

	char *dir_path = g_path_get_dirname(expanded);
	char *pat      = g_path_get_basename(expanded);
	g_free(expanded);

	GSList *opts = NULL;
	expand_dir(dir_path, pat, pop, pop->depth, &opts);

	g_free(dir_path);
	g_free(pat);

	return g_slist_sort(opts, option_alpha_cmp);
}

/* GFileMonitor callback — re-expands the section's populate source on change */
static void on_glob_changed(GFileMonitor *mon, GFile *file, GFile *other,
			    GFileMonitorEvent event, gpointer user_data)
{
	(void)mon; (void)file; (void)other; (void)event;

	struct monitor_ctx *ctx = (struct monitor_ctx *)user_data;

	g_slist_free_full(ctx->sec->options, option_dalloc);
	ctx->sec->options = NULL;

	for(GSList *pl = ctx->sec->populates; pl; pl = pl->next) {
		struct populate *pop = (struct populate *)pl->data;
		if(0 == strcmp(pop->from, "glob"))
			ctx->sec->options = g_slist_concat(ctx->sec->options,
							   expand_glob(pop));
	}
}

/* install GFileMonitor on dir_path and all subdirs up to depth_left.
 * each monitor gets a monitor_ctx with sec, config, pop as user_data.
 * monitors are appended to sec->monitors.
 */
static void watch_dir(const char *dir_path, struct section *sec,
		      struct config *config, struct populate *pop,
		      int depth_left)
{
	GFile  *gfile = g_file_new_for_path(dir_path);
	GError *err   = NULL;

	GFileMonitor *mon = g_file_monitor_directory(gfile,
						      G_FILE_MONITOR_NONE,
						      NULL, &err);
	g_object_unref(gfile);

	if(!mon) {
		fprintf(stderr, "[!] watch error: %s\n", err ? err->message : dir_path);
		if(err) g_error_free(err);
		return;
	}

	struct monitor_ctx *ctx = malloc(sizeof(struct monitor_ctx));
	if(ctx) {
		ctx->sec    = sec;
		ctx->config = config;
		ctx->pop    = pop;
		g_signal_connect_data(mon, "changed",
				      G_CALLBACK(on_glob_changed), ctx,
				      (GClosureNotify)free, 0);
	}

	sec->monitors = g_slist_prepend(sec->monitors, mon);

	if(0 == depth_left)
		return;

	GDir *dir = g_dir_open(dir_path, 0, NULL);
	if(!dir)
		return;

	const char *fname;
	for(; NULL != (fname = g_dir_read_name(dir)); ) {
		char *subpath = g_strconcat(dir_path, G_DIR_SEPARATOR_S, fname, NULL);
		if(g_file_test(subpath, G_FILE_TEST_IS_DIR))
			watch_dir(subpath, sec, config, pop,
				  0 < depth_left ? depth_left - 1 : -1);
		g_free(subpath);
	}
	g_dir_close(dir);
}

/* parse populate blocks from a UCL section scope into a GSList of struct populate */
static GSList *parse_populates(const ucl_object_t *scope)
{
	GSList *pops = NULL;

	ucl_object_iter_t it = ucl_object_iterate_new(scope);
	const ucl_object_t *cur;

	for(; NULL != (cur = ucl_object_iterate_safe(it, true)); ) {
		if(0 != strcmp(ucl_object_key(cur), "populate"))
			continue;

		const ucl_object_t *from_obj = ucl_object_lookup(cur, "from");
		if(!from_obj)
			continue;

		struct populate *pop = malloc(sizeof(struct populate));
		if(!pop)
			continue;

		pop->from = strdup(ucl_object_tostring(from_obj));

		const ucl_object_t *pat = ucl_object_lookup(cur, "pattern");
		pop->pattern = strdup(pat ? ucl_object_tostring(pat) : "");

		const ucl_object_t *w = ucl_object_lookup(cur, "watch");
		pop->watch = w && ucl_object_toboolean(w);

		const ucl_object_t *d = ucl_object_lookup(cur, "depth");
		pop->depth = d ? (int)ucl_object_toint(d) : 0;

		const ucl_object_t *h = ucl_object_lookup(cur, "hierarchy");
		pop->hierarchy = h && ucl_object_toboolean(h);

		const ucl_object_t *item = ucl_object_lookup(cur, "item");
		if(item) {
			const ucl_object_t *lbl = ucl_object_lookup(item, "label");
			const ucl_object_t *cmd = ucl_object_lookup(item, "command");
			pop->label_tpl   = strdup(lbl ? ucl_object_tostring(lbl) : "{filename}");
			pop->command_tpl = strdup(cmd ? ucl_object_tostring(cmd) : "");
		} else {
			pop->label_tpl   = strdup("{filename}");
			pop->command_tpl = strdup("");
		}

		pops = g_slist_prepend(pops, pop);
	}

	ucl_object_iterate_free(it);
	return g_slist_reverse(pops);
}

/* parse all item and section blocks from scope into a GSList of struct section.
 * top-level items become an anonymous section (label == NULL).
 */
static GSList *parse_sections(const ucl_object_t *scope, struct config *config) {
	GSList *ordered = NULL;
	GSList *unordered = NULL;

	/* anonymous section for top-level items */
	GSList *flat_opts = parse_options(scope, 0);
	if(flat_opts) {
		struct section *anon = malloc(sizeof(struct section));
		if(anon) {
			anon->label      = NULL;
			anon->options    = flat_opts;
			anon->populates  = NULL;
			anon->monitors   = NULL;
			anon->order      = -1;
			anon->expanded   = false;
			anon->show_label = true;
			anon->separators = SEPARATORS_BOTH;
			unordered = g_slist_prepend(unordered, anon);
		}
	}

	/* named section blocks */
	ucl_object_iter_t it = ucl_object_iterate_new(scope);
	const ucl_object_t *cur;

	for(; NULL != (cur = ucl_object_iterate_safe(it, true)); ) {
		if(strcmp(ucl_object_key(cur), "section") != 0)
			continue;

		ucl_object_iter_t iit = NULL;
		const ucl_object_t *elem;
		for(; NULL != (elem = ucl_iterate_object(cur, &iit, false)); ) {
			ucl_object_iter_t iiit = NULL;
			const ucl_object_t *sec_obj;
			for(; NULL != (sec_obj = ucl_iterate_object(elem, &iiit, true)); ) {
				struct section *sec = malloc(sizeof(struct section));
				if(!sec)
					continue;

				const char *key = ucl_object_key(sec_obj);
				sec->label    = strdup(key ? key : "");
				sec->options  = parse_options(sec_obj, 1);
				sec->monitors = NULL;

				sec->populates = parse_populates(sec_obj);
				for(GSList *pl = sec->populates; pl; pl = pl->next) {
					struct populate *pop = (struct populate *)pl->data;
					if(0 != strcmp(pop->from, "glob"))
						continue;
					sec->options = g_slist_concat(sec->options,
								      expand_glob(pop));
					if(!pop->watch)
						continue;
					char *dir_path = NULL;
					if('~' == pop->pattern[0])
						dir_path = g_strconcat(g_get_home_dir(),
								       pop->pattern + 1, NULL);
					else
						dir_path = g_strdup(pop->pattern);
					char *base = g_path_get_dirname(dir_path);
					g_free(dir_path);
					watch_dir(base, sec, config, pop, pop->depth);
					g_free(base);
				}

				const ucl_object_t *exp = ucl_object_lookup(sec_obj, "expanded");
				sec->expanded = exp && ucl_object_toboolean(exp);

				const ucl_object_t *sl = ucl_object_lookup(sec_obj, "show_label");
				sec->show_label = !sl || ucl_object_toboolean(sl);

				const ucl_object_t *sp = ucl_object_lookup(sec_obj, "separators");
				if(!sp || (ucl_object_type(sp) == UCL_BOOLEAN && ucl_object_toboolean(sp))) {
					sec->separators = SEPARATORS_BOTH;
				} else if(ucl_object_type(sp) == UCL_STRING) {
					const char *sv = ucl_object_tostring(sp);
					if(strcmp(sv, "top") == 0)
						sec->separators = SEPARATORS_TOP;
					else if(strcmp(sv, "bottom") == 0)
						sec->separators = SEPARATORS_BOTTOM;
					else if(strcmp(sv, "none") == 0)
						sec->separators = SEPARATORS_NONE;
					else
						sec->separators = SEPARATORS_BOTH;
				} else {
					sec->separators = SEPARATORS_NONE;
				}

				const ucl_object_t *ord = ucl_object_lookup(sec_obj, "order");
				if(ord) {
					sec->order = (int)ucl_object_toint(ord);
					ordered = g_slist_prepend(ordered, sec);
				} else {
					sec->order = -1;
					unordered = g_slist_prepend(unordered, sec);
				}
			}
		}
	}

	ucl_object_iterate_free(it);

	ordered   = g_slist_sort(ordered, section_order_cmp);
	unordered = g_slist_reverse(unordered);

	return g_slist_concat(ordered, unordered);
}

static struct config *parse_scope(const char *config_path, const char *name,
				  const ucl_object_t *scope) {
	struct config *config = malloc(sizeof(struct config));
	if(!config) {
		fprintf(stderr, "load_config: couldn't allocate config\n");
		return NULL;
	}

	config->config_path = strdup(config_path);
	config->name        = name ? strdup(name) : NULL;
	config->icon_path   = NULL;
	config->tooltip     = NULL;
	config->sections    = NULL;
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

	config->sections = parse_sections(scope, config);

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
		struct config *config = parse_scope(config_path, NULL, root);
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
					struct config *config = parse_scope(config_path,
									    ucl_object_key(inst),
									    inst);
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
	free(config->name);
	free(config->icon_path);
	free(config->tooltip);

	g_slist_free_full(config->sections, section_dalloc);

	free(config);
}
