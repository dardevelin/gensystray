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
		fprintf(stderr, "gensystray: HOME not set, cannot locate config\n");
		return NULL;
	}

	size_t len = strlen(home) + 1
	           + strlen(def_config_path) + 1
	           + strlen(def_config_file) + 1;

	char *path = malloc(len);
	if(NULL == path) {
		fprintf(stderr, "gensystray: out of memory\n");
		return NULL;
	}

	snprintf(path, len, "%s/%s/%s", home, def_config_path, def_config_file);
	return path;
}

/* safe wrapper for ucl_object_tostring — returns fallback instead of
 * NULL when the object is not a string type (boolean, integer, etc.).
 * prevents strdup(NULL) crashes throughout the parser.
 */
static const char *ucl_tostring_safe(const ucl_object_t *obj,
				     const char *fallback)
{
	const char *s = ucl_object_tostring(obj);
	return s ? s : fallback;
}

/* warn about unrecognized keys in a UCL object scope.
 * known is a NULL-terminated array of accepted key names.
 * scope_desc is used in the warning message, e.g. "tray", "section 'Notes'".
 */
static void warn_unknown_keys(const ucl_object_t *obj, const char **known,
			      const char *scope_desc)
{
	if(!obj)
		return;
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur;
	for(; NULL != (cur = ucl_iterate_object(obj, &it, true)); ) {
		const char *key = ucl_object_key(cur);
		if(!key)
			continue;
		bool found = false;
		for(const char **k = known; *k; k++) {
			if(0 == strcmp(key, *k)) {
				found = true;
				break;
			}
		}
		if(!found)
			fprintf(stderr, "gensystray: %s: unknown key '%s' (ignored)\n",
			        scope_desc, key);
	}
}

/* sanitize a string into a signal name segment.
 * rules: ASCII special chars → '_' (collapsed), ASCII letters lowercased,
 * non-ASCII bytes passed through as-is (UTF-8 safe), leading/trailing '_' stripped.
 * examples: "CPU Usage" → "cpu_usage", "Déjà vu" → "déjà_vu", "battery %" → "battery"
 * returns a malloc'd string, caller must free.
 */
static char *sanitize_signal_name(const char *s)
{
	if(!s || '\0' == s[0]) {
		fprintf(stderr, "gensystray: signal name is empty, "
		        "using 'unnamed' (may cause collisions)\n");
		return strdup("unnamed");
	}

	size_t len = strlen(s);
	char  *buf = malloc(len + 1);
	if(!buf)
		return strdup("unnamed");

	size_t out  = 0;
	bool   prev = false;   /* true if last written char was '_' */

	for(size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)s[i];

		if(127 < c) {
			buf[out++] = (char)c;
			prev = false;
			continue;
		}

		if('a' <= c && c <= 'z') {
			buf[out++] = (char)c;
			prev = false;
			continue;
		}

		if('0' <= c && c <= '9') {
			buf[out++] = (char)c;
			prev = false;
			continue;
		}

		if('A' <= c && c <= 'Z') {
			buf[out++] = (char)(c + 32);
			prev = false;
			continue;
		}

		if(!prev && 0 < out) {
			buf[out++] = '_';
			prev = true;
		}
	}

	/* strip trailing '_' */
	for(; 0 < out && '_' == buf[out - 1]; )
		out--;

	buf[out] = '\0';

	if(0 == out) {
		free(buf);
		fprintf(stderr, "gensystray: signal name '%s' sanitizes to empty, "
		        "using 'unnamed' (may cause collisions)\n", s);
		return strdup("unnamed");
	}

	return buf;
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
		fprintf(stderr, "gensystray: refresh: invalid value '%s' (expected number + unit, e.g. \"2s\")\n", s);
		return 0;
	}

	long ms = 0;
	if(0 == strcmp(end, "ms"))      ms = val;
	else if(0 == strcmp(end, "s"))  ms = val * 1000;
	else if(0 == strcmp(end, "m"))  ms = val * 60 * 1000;
	else if(0 == strcmp(end, "h"))  ms = val * 3600 * 1000;
	else {
		fprintf(stderr, "gensystray: refresh: unknown unit in '%s' (use ms, s, m, h)\n", s);
		return 0;
	}

	/* guard against overflow and enforce minimum 100ms */
	if(ms > (long)G_MAXUINT || ms < 0) {
		fprintf(stderr, "gensystray: refresh: value '%s' overflows, clamped to 24h\n", s);
		ms = 24L * 3600 * 1000;
	}
	if(ms < 100) {
		fprintf(stderr, "gensystray: refresh: value '%s' below minimum, clamped to 100ms\n", s);
		ms = 100;
	}

	return (guint)ms;
}

/* normalize a UCL command value into a heap-allocated argv array.
 * UCL array  → each element becomes an argv entry.
 * UCL string → wrapped as ["sh", "-c", str, NULL] for shell execution.
 * returns NULL if obj is NULL or empty.
 * caller must free with g_strfreev.
 */
static char **parse_argv(const ucl_object_t *obj)
{
	if(!obj)
		return NULL;

	if(UCL_ARRAY == ucl_object_type(obj)) {
		ucl_object_iter_t it = NULL;
		const ucl_object_t *elem;
		GPtrArray *arr = g_ptr_array_new();
		for(; NULL != (elem = ucl_iterate_object(obj, &it, true)); )
			g_ptr_array_add(arr, g_strdup(ucl_tostring_safe(elem, "")));
		g_ptr_array_add(arr, NULL);
		return (char **)g_ptr_array_free(arr, false);
	}

	/* string or heredoc — wrap in shell -c.
	 * shebang (#!) on the first line overrides the shell for this item.
	 * otherwise falls back to $SHELL env var, then "sh" as last resort.
	 */
	const char *s = ucl_object_tostring(obj);
	if(!s || '\0' == s[0])
		return NULL;

	char *shell = NULL;

	if('#' == s[0] && '!' == s[1]) {
		/* extract interpreter from shebang line */
		const char *end = strchr(s + 2, '\n');
		size_t len = end ? (size_t)(end - (s + 2)) : strlen(s + 2);
		shell = g_strndup(s + 2, len);
		/* skip past the shebang line for the script body */
		s = end ? end + 1 : s + 2 + len;
	}

	if(!shell) {
		const char *env_shell = getenv("SHELL");
		shell = g_strdup(env_shell ? env_shell : "sh");
	}

	char **argv = g_new(char *, 4);
	argv[0] = shell;
	argv[1] = g_strdup("-c");
	argv[2] = g_strdup(s);
	argv[3] = NULL;
	return argv;
}

/* free a GSList of char** argv entries */
static void command_list_free(GSList *list) {
	for(GSList *l = list; l; l = l->next)
		g_strfreev((char **)l->data);
	g_slist_free(list);
}

/* parse a UCL command value into a GSList of char** argv.
 * flat array ["cmd", "arg"]      → one argv entry.
 * array of arrays [["a","b"],…]  → one argv entry per sub-array.
 * string/heredoc                 → one argv entry via shell.
 * returns NULL if obj is NULL.
 */
static GSList *parse_command_list(const ucl_object_t *obj)
{
	if(!obj)
		return NULL;

	if(UCL_ARRAY == ucl_object_type(obj)) {
		/* peek at first element to detect array-of-arrays */
		ucl_object_iter_t peek = NULL;
		const ucl_object_t *first = ucl_iterate_object(obj, &peek, true);

		if(first && UCL_ARRAY == ucl_object_type(first)) {
			/* array of arrays — each sub-array is one argv */
			GSList *list = NULL;
			ucl_object_iter_t it = NULL;
			const ucl_object_t *sub;
			for(; NULL != (sub = ucl_iterate_object(obj, &it, true)); ) {
				char **argv = parse_argv(sub);
				if(argv)
					list = g_slist_append(list, argv);
			}
			return list;
		}

		/* flat array — single argv */
		char **argv = parse_argv(obj);
		return argv ? g_slist_append(NULL, argv) : NULL;
	}

	/* string or heredoc — single argv via shell */
	char **argv = parse_argv(obj);
	return argv ? g_slist_append(NULL, argv) : NULL;
}

/* parse an emit block from a UCL scope. returns NULL if not present or invalid.
 * context_desc is used in error messages, e.g. "item 'Deploy'" or "on exit 0".
 */
static struct emit *parse_emit(const ucl_object_t *scope,
			       const char *context_desc)
{
	const ucl_object_t *emit_obj = ucl_object_lookup(scope, "emit");
	if(!emit_obj)
		return NULL;

	if(UCL_OBJECT != ucl_object_type(emit_obj)) {
		fprintf(stderr, "gensystray: %s: emit must be a block, "
		        "e.g. emit { signal = \"name\"; value = \"payload\" }\n",
		        context_desc);
		return NULL;
	}

	static const char *known_emit[] = { "signal", "value", NULL };
	warn_unknown_keys(emit_obj, known_emit, context_desc);

	const ucl_object_t *sig = ucl_object_lookup(emit_obj, "signal");
	if(!sig) {
		fprintf(stderr, "gensystray: %s: emit block requires 'signal'\n",
		        context_desc);
		return NULL;
	}

	const char *sig_str = ucl_object_tostring(sig);
	if(!sig_str || '\0' == sig_str[0]) {
		fprintf(stderr, "gensystray: %s: emit.signal is empty\n",
		        context_desc);
		return NULL;
	}

	struct emit *em = malloc(sizeof(struct emit));
	if(!em)
		return NULL;

	em->signal = strdup(sig_str);
	const ucl_object_t *val = ucl_object_lookup(emit_obj, "value");
	em->value = val ? strdup(ucl_tostring_safe(val, "")) : NULL;

	return em;
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

static void emit_dalloc(struct emit *em) {
	if(!em) return;
	free(em->signal);
	free(em->value);
	free(em);
}

static void on_block_dalloc(void *data) {
	struct on_block *ob = (struct on_block *)data;
	free(ob->output_match);
	free(ob->label);
	command_list_free(ob->commands);
	emit_dalloc(ob->emit);
	free(ob);
}

static void live_dalloc(struct live *lv) {
	if(!lv)
		return;
	g_strfreev(lv->update_label_argv);
	g_strfreev(lv->update_tray_icon_argv);
	command_list_free(lv->commands);
	g_slist_free_full(lv->on_blocks, on_block_dalloc);
	free(lv->signal_name);
	free(lv->live_output);
	free(lv);
}

static void on_emit_block_dalloc(void *data) {
	struct on_emit_block *eb = (struct on_emit_block *)data;
	free(eb->signal_name);
	free(eb->command_tpl);
	emit_dalloc(eb->emit);
	free(eb);
}

static void option_dalloc(void *data) {
	struct option *opt = (struct option *)data;
	free(opt->name);
	command_list_free(opt->commands);
	emit_dalloc(opt->emit);
	live_dalloc(opt->live);
	g_slist_free_full(opt->subopts, option_dalloc);
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

static void on_watch_block_dalloc(void *data) {
	struct on_watch_block *wb = (struct on_watch_block *)data;
	free(wb->command_tpl);
	emit_dalloc(wb->emit);
	free(wb->signal_name);
	free(wb);
}

static void section_dalloc(void *data) {
	struct section *sec = (struct section *)data;
	free(sec->label);
	g_slist_free_full(sec->options, option_dalloc);
	g_slist_free_full(sec->populates, populate_dalloc);
	g_slist_free_full(sec->monitors, (GDestroyNotify)g_object_unref);
	g_slist_free_full(sec->on_watch_blocks,
			  (GDestroyNotify)on_watch_block_dalloc);
	g_slist_free_full(sec->on_emit_blocks,
			  (GDestroyNotify)on_emit_block_dalloc);
	free(sec);
}

/* scan raw config text for on blocks belonging to the live item named item_name.
 *
 * WHY A CUSTOM PARSER: UCL merges duplicate keys, so multiple "on exit N { }"
 * or "on output "str" { }" lines cannot be expressed as UCL — the second key
 * silently overwrites the first.  We pre-process the raw config text instead,
 * scoping to the named item's live { } region, then feed each block body back
 * to UCL for the label/command values where no duplication occurs.
 *
 * recognises two forms (UTF-8 safe — multi-byte sequences pass through):
 *   on exit N   { label = "..." command = ... }
 *   on output "str" { label = "..." command = ... }
 * returns a GSList of struct on_block in declaration order.
 */
static GSList *parse_on_blocks(const char *raw_text, const char *item_name)
{
	if(!raw_text || !item_name)
		return NULL;

	GSList *list = NULL;

	/* locate item "item_name" { in the raw text */
	const char *scope_start = NULL;
	const char *scan = raw_text;
	for(; '\0' != *scan; ) {
		/* skip whitespace */
		for(; ' ' == *scan || '\t' == *scan; scan++) {}

		/* look for: item "item_name" or item 'item_name' */
		if(0 == strncmp(scan, "item", 4) && (' ' == scan[4] || '\t' == scan[4])) {
			scan += 4;
			for(; ' ' == *scan || '\t' == *scan; scan++) {}
			char delim = '\0';
			if('"' == *scan || '\'' == *scan)
				delim = *scan++;
			const char *ns = scan;
			if('\0' != delim) {
				for(; '\0' != *scan && *scan != delim; scan++) {}
			} else {
				for(; '\0' != *scan && ' ' != *scan && '\t' != *scan
				    && '{' != *scan && '\n' != *scan; scan++) {}
			}
			size_t nlen = (size_t)(scan - ns);
			if('\0' != delim && *scan == delim)
				scan++;
			if(nlen == strlen(item_name) && 0 == strncmp(ns, item_name, nlen)) {
				/* found the item — advance to its opening '{' */
				for(; '\0' != *scan && '{' != *scan && '\n' != *scan; scan++) {}
				if('{' == *scan) {
					scope_start = scan + 1;
				}
				break;
			}
		}

		/* skip to next line */
		for(; '\0' != *scan && '\n' != *scan; scan++) {}
		if('\n' == *scan) scan++;
	}

	if(!scope_start)
		return NULL;

	/* find the end of the item's brace region */
	int item_depth = 1;
	const char *scope_end = scope_start;
	for(; '\0' != *scope_end && 0 < item_depth; scope_end++) {
		if('{' == *scope_end)      item_depth++;
		else if('}' == *scope_end) item_depth--;
	}

	/* within item region, find the live { block */
	const char *live_start = NULL;
	scan = scope_start;
	for(; scan < scope_end; ) {
		for(; scan < scope_end && (' ' == *scan || '\t' == *scan); scan++) {}
		if(0 == strncmp(scan, "live", 4)
		   && (scan + 4 >= scope_end
		       || ' ' == scan[4] || '\t' == scan[4] || '{' == scan[4])) {
			scan += 4;
			for(; scan < scope_end && (' ' == *scan || '\t' == *scan); scan++) {}
			if(scan < scope_end && '{' == *scan) {
				live_start = scan + 1;
				break;
			}
		}
		for(; scan < scope_end && '\n' != *scan; scan++) {}
		if(scan < scope_end && '\n' == *scan) scan++;
	}

	if(!live_start)
		return NULL;

	/* find end of live { block */
	int live_depth = 1;
	const char *live_end = live_start;
	for(; '\0' != *live_end && 0 < live_depth && live_end < scope_end; live_end++) {
		if('{' == *live_end)      live_depth++;
		else if('}' == *live_end) live_depth--;
	}

	/* count lines from start of file to live_start for error reporting */
	int line_num = 1;
	for(const char *c = raw_text; c < live_start; c++) {
		if('\n' == *c) line_num++;
	}

	/* now scan only within the live block for on lines */
	const char *p = live_start;
	for(; p < live_end && '\0' != *p; ) {
		/* skip whitespace */
		for(; p < live_end && (' ' == *p || '\t' == *p); p++) {}

		/* match "on exit" or "on output" */
		bool is_exit   = (0 == strncmp(p, "on exit",   7) && (' ' == p[7] || '\t' == p[7]));
		bool is_output = (0 == strncmp(p, "on output", 9) && (' ' == p[9] || '\t' == p[9] || '"' == p[9]));

		if(!is_exit && !is_output) {
			/* skip to next line */
			for(; p < live_end && '\0' != *p && '\n' != *p; p++) {}
			if(p < live_end && '\n' == *p) { p++; line_num++; }
			continue;
		}

		int block_line = line_num;

		struct on_block *ob = malloc(sizeof(struct on_block));
		if(!ob)
			break;
		ob->label        = NULL;
		ob->output_match = NULL;
		ob->commands     = NULL;
		ob->emit         = NULL;

		if(is_exit) {
			p += 7;
			for(; ' ' == *p || '\t' == *p; p++) {}
			ob->kind = ON_EXIT;
			char *endptr = NULL;
			ob->exit_code = (int)strtol(p, &endptr, 10);
			if(endptr == p) {
				fprintf(stderr, "gensystray: line %d: 'on exit' expects a number, e.g. on exit 0 { } (item '%s')\n",
				        block_line, item_name);
				free(ob);
				for(; p < live_end && '\0' != *p && '\n' != *p; p++) {}
				if(p < live_end && '\n' == *p) { p++; line_num++; }
				continue;
			}
			p = endptr;
		} else {
			p += 9;
			for(; ' ' == *p || '\t' == *p; p++) {}
			ob->kind      = ON_OUTPUT;
			ob->exit_code = 0;
			/* match string — quoted or unquoted */
			if('"' == *p) {
				p++;
				const char *start = p;
				for(; '\0' != *p && '"' != *p && '\n' != *p; p++) {
					/* skip escaped quote */
					if('\\' == *p && '"' == *(p + 1))
						p++;
				}
				if('"' != *p) {
					fprintf(stderr, "gensystray: line %d: unterminated quote in 'on output', e.g. on output \"str\" { } (item '%s')\n",
					        block_line, item_name);
					free(ob);
					if(p < live_end && '\n' == *p) { p++; line_num++; }
					continue;
				}
				ob->output_match = g_strndup(start, (gsize)(p - start));
				p++; /* skip closing '"' */
			} else {
				const char *start = p;
				for(; '\0' != *p && ' ' != *p && '\t' != *p && '{' != *p; p++) {}
				if(p == start) {
					fprintf(stderr, "gensystray: line %d: 'on output' expects a match string, e.g. on output \"str\" { } (item '%s')\n",
					        block_line, item_name);
					free(ob);
					for(; p < live_end && '\0' != *p && '\n' != *p; p++) {}
					if(p < live_end && '\n' == *p) { p++; line_num++; }
					continue;
				}
				ob->output_match = g_strndup(start, (gsize)(p - start));
			}
		}

		/* skip to opening brace */
		for(; '\0' != *p && '{' != *p && '\n' != *p; p++) {}
		if('{' != *p) {
			fprintf(stderr, "gensystray: line %d: 'on' block missing opening '{' (item '%s')\n",
			        block_line, item_name);
			free(ob->output_match);
			free(ob);
			if('\n' == *p) { p++; line_num++; }
			continue;
		}
		p++; /* skip '{' */

		/* collect brace content, tracking nesting depth */
		int depth = 1;
		const char *body_start = p;
		for(; '\0' != *p && 0 < depth; p++) {
			if('{' == *p) depth++;
			else if('}' == *p) depth--;
			else if('\n' == *p) line_num++;
		}

		if(0 != depth) {
			fprintf(stderr, "gensystray: line %d: unclosed '}' in 'on' block (item '%s')\n",
			        block_line, item_name);
			free(ob->output_match);
			free(ob);
			continue;
		}

		/* p now points just past the closing '}' */
		gsize body_len = (gsize)(p - body_start - 1); /* exclude closing '}' */

		/* parse the brace content with UCL to get label and command */
		char *body = g_strndup(body_start, body_len);
		struct ucl_parser *bp = ucl_parser_new(0);
		if(!ucl_parser_add_string(bp, body, 0)) {
			fprintf(stderr, "gensystray: line %d: syntax error inside 'on' block: %s (item '%s')\n",
			        block_line, ucl_parser_get_error(bp), item_name);
			ucl_parser_free(bp);
			g_free(body);
			free(ob->output_match);
			free(ob);
			continue;
		}

		ucl_object_t *br = ucl_parser_get_object(bp);
		if(br) {
			const ucl_object_t *lbl = ucl_object_lookup(br, "label");
			const ucl_object_t *cmd = ucl_object_lookup(br, "command");
			ob->label    = lbl ? strdup(ucl_tostring_safe(lbl, "")) : NULL;
			ob->commands = parse_command_list(cmd);

			char emit_ctx[128];
			snprintf(emit_ctx, sizeof(emit_ctx),
			         "item '%s' > on %s", item_name,
			         ob->kind == ON_EXIT ? "exit" : "output");
			ob->emit = parse_emit(br, emit_ctx);

			ucl_object_unref(br);
		}
		ucl_parser_free(bp);
		g_free(body);

		list = g_slist_append(list, ob);
	}

	return list;
}

/* parse on watch-* blocks from raw config text for the named section.
 *
 * scans the raw text for the section's brace region, then looks for:
 *   on watch-change  { command = "..." }
 *   on watch-create  { command = "..." }
 *   on watch-delete  { command = "..." }
 *
 * unknown "on watch-*" suffixes produce a warning.
 * unknown "on <other>" keywords inside sections are warned too.
 * {filepath} and {filename} tokens in command are stored as-is
 * and substituted at event time.
 *
 * returns a GSList of struct on_watch_block in declaration order.
 */
static GSList *parse_on_watch_blocks(const char *raw_text,
				     const char *section_name)
{
	if(!raw_text || !section_name)
		return NULL;

	GSList *list = NULL;

	/* locate section "section_name" { in raw text */
	const char *scope_start = NULL;
	const char *scan = raw_text;
	for(; '\0' != *scan; ) {
		for(; ' ' == *scan || '\t' == *scan; scan++) {}

		if(0 == strncmp(scan, "section", 7)
		   && (' ' == scan[7] || '\t' == scan[7])) {
			scan += 7;
			for(; ' ' == *scan || '\t' == *scan; scan++) {}
			char delim = '\0';
			if('"' == *scan || '\'' == *scan)
				delim = *scan++;
			const char *ns = scan;
			if('\0' != delim) {
				for(; '\0' != *scan && *scan != delim; scan++) {}
			} else {
				for(; '\0' != *scan && ' ' != *scan && '\t' != *scan
				    && '{' != *scan && '\n' != *scan; scan++) {}
			}
			size_t nlen = (size_t)(scan - ns);
			if('\0' != delim && *scan == delim)
				scan++;
			if(nlen == strlen(section_name)
			   && 0 == strncmp(ns, section_name, nlen)) {
				for(; '\0' != *scan && '{' != *scan && '\n' != *scan; scan++) {}
				if('{' == *scan)
					scope_start = scan + 1;
				break;
			}
		}

		for(; '\0' != *scan && '\n' != *scan; scan++) {}
		if('\n' == *scan) scan++;
	}

	if(!scope_start)
		return NULL;

	/* find end of section brace region */
	int depth = 1;
	const char *scope_end = scope_start;
	for(; '\0' != *scope_end && 0 < depth; scope_end++) {
		if('{' == *scope_end)      depth++;
		else if('}' == *scope_end) depth--;
	}

	/* count lines for error reporting */
	int line_num = 1;
	for(const char *c = raw_text; c < scope_start; c++) {
		if('\n' == *c) line_num++;
	}

	/* scan section region for "on watch-*" lines */
	const char *p = scope_start;
	for(; p < scope_end && '\0' != *p; ) {
		for(; p < scope_end && (' ' == *p || '\t' == *p); p++) {}

		/* match "on " prefix */
		if(0 != strncmp(p, "on ", 3) && 0 != strncmp(p, "on\t", 3)) {
			for(; p < scope_end && '\n' != *p; p++) {}
			if(p < scope_end && '\n' == *p) { p++; line_num++; }
			continue;
		}

		int block_line = line_num;
		p += 3;
		for(; ' ' == *p || '\t' == *p; p++) {}

		/* skip known item-level on blocks (handled by parse_on_blocks) */
		if(0 == strncmp(p, "exit", 4) && (' ' == p[4] || '\t' == p[4])) {
			for(; p < scope_end && '\n' != *p; p++) {}
			if(p < scope_end && '\n' == *p) { p++; line_num++; }
			continue;
		}
		if(0 == strncmp(p, "output", 6)
		   && (' ' == p[6] || '\t' == p[6] || '"' == p[6])) {
			for(; p < scope_end && '\n' != *p; p++) {}
			if(p < scope_end && '\n' == *p) { p++; line_num++; }
			continue;
		}

		/* identify watch-* kind */
		on_watch_kind kind;
		bool matched = false;
		if(0 == strncmp(p, "watch-change", 12)
		   && (' ' == p[12] || '\t' == p[12] || '{' == p[12])) {
			kind = ON_WATCH_CHANGE;
			p += 12;
			matched = true;
		} else if(0 == strncmp(p, "watch-create", 12)
		          && (' ' == p[12] || '\t' == p[12] || '{' == p[12])) {
			kind = ON_WATCH_CREATE;
			p += 12;
			matched = true;
		} else if(0 == strncmp(p, "watch-delete", 12)
		          && (' ' == p[12] || '\t' == p[12] || '{' == p[12])) {
			kind = ON_WATCH_DELETE;
			p += 12;
			matched = true;
		} else if(0 == strncmp(p, "watch-", 6)) {
			/* unknown watch- suffix */
			const char *ws = p;
			for(; p < scope_end && ' ' != *p && '\t' != *p
			    && '{' != *p && '\n' != *p; p++) {}
			fprintf(stderr, "gensystray: line %d: section '%s': "
			        "unknown 'on %.*s' (expected watch-change, "
			        "watch-create, or watch-delete)\n",
			        block_line, section_name, (int)(p - ws), ws);
			for(; p < scope_end && '\n' != *p; p++) {}
			if(p < scope_end && '\n' == *p) { p++; line_num++; }
			continue;
		} else {
			/* unknown on keyword entirely */
			const char *ws = p;
			for(; p < scope_end && ' ' != *p && '\t' != *p
			    && '{' != *p && '\n' != *p; p++) {}
			fprintf(stderr, "gensystray: line %d: section '%s': "
			        "unknown 'on %.*s' (ignored)\n",
			        block_line, section_name, (int)(p - ws), ws);
			for(; p < scope_end && '\n' != *p; p++) {}
			if(p < scope_end && '\n' == *p) { p++; line_num++; }
			continue;
		}

		if(!matched) {
			for(; p < scope_end && '\n' != *p; p++) {}
			if(p < scope_end && '\n' == *p) { p++; line_num++; }
			continue;
		}

		/* skip to opening brace */
		for(; '\0' != *p && '{' != *p && '\n' != *p; p++) {}
		if('{' != *p) {
			fprintf(stderr, "gensystray: line %d: section '%s': "
			        "'on watch-*' block missing opening '{'\n",
			        block_line, section_name);
			if('\n' == *p) { p++; line_num++; }
			continue;
		}
		p++;

		/* collect brace body */
		int bdepth = 1;
		const char *body_start = p;
		for(; '\0' != *p && 0 < bdepth; p++) {
			if('{' == *p) bdepth++;
			else if('}' == *p) bdepth--;
			else if('\n' == *p) line_num++;
		}

		if(0 != bdepth) {
			fprintf(stderr, "gensystray: line %d: section '%s': "
			        "unclosed '}' in 'on watch-*' block\n",
			        block_line, section_name);
			continue;
		}

		gsize body_len = (gsize)(p - body_start - 1);
		char *body = g_strndup(body_start, body_len);

		/* parse body with UCL for the command field */
		struct ucl_parser *bp = ucl_parser_new(0);
		if(!ucl_parser_add_string(bp, body, 0)) {
			fprintf(stderr, "gensystray: line %d: section '%s': "
			        "syntax error in 'on watch-*' block: %s\n",
			        block_line, section_name,
			        ucl_parser_get_error(bp));
			ucl_parser_free(bp);
			g_free(body);
			continue;
		}

		ucl_object_t *br = ucl_parser_get_object(bp);
		char *command_tpl = NULL;
		struct emit *wb_emit = NULL;
		if(br) {
			static const char *known_watch_body[] = {
				"command", "emit", NULL
			};
			char wbd[128];
			snprintf(wbd, sizeof(wbd), "section '%s' > on watch-*",
			         section_name);
			warn_unknown_keys(br, known_watch_body, wbd);

			const ucl_object_t *cmd = ucl_object_lookup(br, "command");
			if(cmd) {
				const char *s = ucl_object_tostring(cmd);
				if(s) {
					command_tpl = strdup(s);
				} else {
					fprintf(stderr, "gensystray: section '%s': "
					        "'on watch-*' command must be a string, "
					        "not an array\n", section_name);
				}
			}
			wb_emit = parse_emit(br, wbd);
			ucl_object_unref(br);
		}
		ucl_parser_free(bp);
		g_free(body);

		if(!command_tpl && !wb_emit)
			continue;

		/* build signal name: watch.<section>.<event> */
		char *sec_san = sanitize_signal_name(section_name);
		const char *event_str = kind == ON_WATCH_CHANGE ? "change"
		                      : kind == ON_WATCH_CREATE ? "create"
		                      : "delete";
		size_t slen = strlen("watch.") + strlen(sec_san)
		            + 1 + strlen(event_str) + 1;
		char *sig = malloc(slen);
		if(sig)
			snprintf(sig, slen, "watch.%s.%s", sec_san, event_str);
		free(sec_san);

		struct on_watch_block *wb = malloc(sizeof(struct on_watch_block));
		if(!wb) {
			free(command_tpl);
			emit_dalloc(wb_emit);
			free(sig);
			continue;
		}
		wb->kind        = kind;
		wb->command_tpl = command_tpl;
		wb->emit        = wb_emit;
		wb->signal_name = sig;
		wb->conn        = 0;

		list = g_slist_append(list, wb);
	}

	return list;
}

/* parse on emit "signal" { command = "..." } blocks from raw config text
 * for the named section. {value} token in command is stored as-is and
 * substituted at event time from the signal payload.
 *
 * returns a GSList of struct on_emit_block in declaration order.
 */
static GSList *parse_on_emit_blocks(const char *raw_text,
				    const char *section_name)
{
	if(!raw_text || !section_name)
		return NULL;

	GSList *list = NULL;

	/* locate section "section_name" { in raw text — reuse same pattern */
	const char *scope_start = NULL;
	const char *scan = raw_text;
	for(; '\0' != *scan; ) {
		for(; ' ' == *scan || '\t' == *scan; scan++) {}
		if(0 == strncmp(scan, "section", 7)
		   && (' ' == scan[7] || '\t' == scan[7])) {
			scan += 7;
			for(; ' ' == *scan || '\t' == *scan; scan++) {}
			char delim = '\0';
			if('"' == *scan || '\'' == *scan)
				delim = *scan++;
			const char *ns = scan;
			if('\0' != delim) {
				for(; '\0' != *scan && *scan != delim; scan++) {}
			} else {
				for(; '\0' != *scan && ' ' != *scan && '\t' != *scan
				    && '{' != *scan && '\n' != *scan; scan++) {}
			}
			size_t nlen = (size_t)(scan - ns);
			if('\0' != delim && *scan == delim)
				scan++;
			if(nlen == strlen(section_name)
			   && 0 == strncmp(ns, section_name, nlen)) {
				for(; '\0' != *scan && '{' != *scan && '\n' != *scan; scan++) {}
				if('{' == *scan)
					scope_start = scan + 1;
				break;
			}
		}
		for(; '\0' != *scan && '\n' != *scan; scan++) {}
		if('\n' == *scan) scan++;
	}

	if(!scope_start)
		return NULL;

	int depth = 1;
	const char *scope_end = scope_start;
	for(; '\0' != *scope_end && 0 < depth; scope_end++) {
		if('{' == *scope_end)      depth++;
		else if('}' == *scope_end) depth--;
	}

	int line_num = 1;
	for(const char *c = raw_text; c < scope_start; c++) {
		if('\n' == *c) line_num++;
	}

	const char *p = scope_start;
	for(; p < scope_end && '\0' != *p; ) {
		for(; p < scope_end && (' ' == *p || '\t' == *p); p++) {}

		/* match "on emit" */
		if(0 != strncmp(p, "on emit", 7)
		   || (' ' != p[7] && '\t' != p[7] && '"' != p[7])) {
			for(; p < scope_end && '\n' != *p; p++) {}
			if(p < scope_end && '\n' == *p) { p++; line_num++; }
			continue;
		}

		int block_line = line_num;
		p += 7;
		for(; ' ' == *p || '\t' == *p; p++) {}

		/* extract signal name — must be quoted */
		if('"' != *p) {
			fprintf(stderr, "gensystray: line %d: section '%s': "
			        "'on emit' expects a quoted signal name, "
			        "e.g. on emit \"signal_name\" { }\n",
			        block_line, section_name);
			for(; p < scope_end && '\n' != *p; p++) {}
			if(p < scope_end && '\n' == *p) { p++; line_num++; }
			continue;
		}
		p++;
		const char *sig_start = p;
		for(; '\0' != *p && '"' != *p && '\n' != *p; p++) {}
		if('"' != *p) {
			fprintf(stderr, "gensystray: line %d: section '%s': "
			        "unterminated quote in 'on emit'\n",
			        block_line, section_name);
			if(p < scope_end && '\n' == *p) { p++; line_num++; }
			continue;
		}
		char *signal_name = g_strndup(sig_start, (gsize)(p - sig_start));
		p++; /* skip closing '"' */

		/* skip to opening brace */
		for(; '\0' != *p && '{' != *p && '\n' != *p; p++) {}
		if('{' != *p) {
			fprintf(stderr, "gensystray: line %d: section '%s': "
			        "'on emit' block missing opening '{'\n",
			        block_line, section_name);
			g_free(signal_name);
			if('\n' == *p) { p++; line_num++; }
			continue;
		}
		p++;

		/* collect brace body */
		int bdepth = 1;
		const char *body_start = p;
		for(; '\0' != *p && 0 < bdepth; p++) {
			if('{' == *p) bdepth++;
			else if('}' == *p) bdepth--;
			else if('\n' == *p) line_num++;
		}

		if(0 != bdepth) {
			fprintf(stderr, "gensystray: line %d: section '%s': "
			        "unclosed '}' in 'on emit' block\n",
			        block_line, section_name);
			g_free(signal_name);
			continue;
		}

		gsize body_len = (gsize)(p - body_start - 1);
		char *body = g_strndup(body_start, body_len);

		struct ucl_parser *bp = ucl_parser_new(0);
		if(!ucl_parser_add_string(bp, body, 0)) {
			fprintf(stderr, "gensystray: line %d: section '%s': "
			        "syntax error in 'on emit' block: %s\n",
			        block_line, section_name,
			        ucl_parser_get_error(bp));
			ucl_parser_free(bp);
			g_free(body);
			g_free(signal_name);
			continue;
		}

		ucl_object_t *br = ucl_parser_get_object(bp);
		char *command_tpl = NULL;
		struct emit *eb_emit = NULL;
		if(br) {
			static const char *known_emit_body[] = {
				"command", "emit", NULL
			};
			char ebd[128];
			snprintf(ebd, sizeof(ebd), "section '%s' > on emit",
			         section_name);
			warn_unknown_keys(br, known_emit_body, ebd);

			const ucl_object_t *cmd = ucl_object_lookup(br, "command");
			if(cmd) {
				const char *s = ucl_object_tostring(cmd);
				if(s) {
					command_tpl = strdup(s);
				} else {
					fprintf(stderr, "gensystray: section '%s': "
					        "'on emit' command must be a string\n",
					        section_name);
				}
			}
			eb_emit = parse_emit(br, ebd);
			ucl_object_unref(br);
		}
		ucl_parser_free(bp);
		g_free(body);

		if(!command_tpl && !eb_emit) {
			g_free(signal_name);
			continue;
		}

		struct on_emit_block *eb = malloc(sizeof(struct on_emit_block));
		if(!eb) {
			free(command_tpl);
			emit_dalloc(eb_emit);
			g_free(signal_name);
			continue;
		}
		eb->signal_name = signal_name;
		eb->command_tpl = command_tpl;
		eb->emit        = eb_emit;
		eb->conn        = 0;

		list = g_slist_append(list, eb);
	}

	return list;
}

/* parse item blocks from a UCL scope into a GSList of struct option.
 * named sections sort alphabetically; anonymous sections use declaration order.
 * across sections: explicit order first, then declaration order.
 */
static GSList *parse_options(const ucl_object_t *scope, int named,
			     const char *raw_text) {
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
					fprintf(stderr, "gensystray: out of memory\n");
					continue;
				}

				const char *key = ucl_object_key(item);
				opt->name = strdup(key ? key : "");

				static const char *known_item[] = {
					"separator", "command", "emit", "live", "order", NULL
				};
				char item_desc[128];
				snprintf(item_desc, sizeof(item_desc), "item '%s'", opt->name);
				warn_unknown_keys(item, known_item, item_desc);

				const ucl_object_t *sep = ucl_object_lookup(item, "separator");
				if(sep && ucl_object_toboolean(sep)) {
					free(opt->name);
					opt->name     = strdup("--");
					opt->commands = NULL;
					opt->emit     = NULL;
					opt->live     = NULL;
					opt->subopts          = NULL;
					opt->subopts_expanded = false;
					opt->order            = -1;
					unordered = g_slist_prepend(unordered, opt);
					continue;
				}

				const ucl_object_t *cmd = ucl_object_lookup(item, "command");
				opt->commands = parse_command_list(cmd);
				opt->emit         = NULL;
				opt->live         = NULL;
				opt->subopts          = NULL;
				opt->subopts_expanded = false;

				{
					char emit_ctx[128];
					snprintf(emit_ctx, sizeof(emit_ctx), "item '%s' > emit", opt->name);
					opt->emit = parse_emit(item, emit_ctx);
				}

				const ucl_object_t *live_blk = ucl_object_lookup(item, "live");
				if(live_blk) {
					static const char *known_live[] = {
						"refresh", "update_label", "update_tray_icon",
						"command", "independent", "signal_name", NULL
					};
					char live_desc[128];
					snprintf(live_desc, sizeof(live_desc),
					         "item '%s' > live", opt->name);
					warn_unknown_keys(live_blk, known_live, live_desc);

					const ucl_object_t *ref = ucl_object_lookup(live_blk, "refresh");
					if(!ref) {
						fprintf(stderr, "gensystray: item '%s': live block requires 'refresh', e.g. refresh = \"1s\"\n", opt->name);
						free(opt->name);
						opt->name         = strdup("[!] missing refresh");
						opt->commands = NULL;
						opt->order        = -1;
						unordered = g_slist_prepend(unordered, opt);
						continue;
					}

					struct live *lv = malloc(sizeof(struct live));
					if(!lv) {
						fprintf(stderr, "gensystray: out of memory\n");
						continue;
					}

					const ucl_object_t *ul  = ucl_object_lookup(live_blk, "update_label");
					const ucl_object_t *uti = ucl_object_lookup(live_blk, "update_tray_icon");
					const ucl_object_t *lc  = ucl_object_lookup(live_blk, "command");
					lv->update_label_argv     = parse_argv(ul);
					lv->update_tray_icon_argv = parse_argv(uti);
					lv->commands              = parse_command_list(lc);
					lv->refresh_ms        = parse_duration_ms(ucl_object_tostring(ref));
					lv->tick_counter          = 0;
					lv->live_output           = NULL;
					lv->timer_id              = 0;
					lv->live_label        = NULL;
					lv->live_conn         = 0;
					lv->last_matched      = NULL;
					lv->on_blocks         = NULL;
					lv->owner             = NULL;

					const ucl_object_t *ind = ucl_object_lookup(live_blk, "independent");
					lv->independent = ind && ucl_object_toboolean(ind);

					const ucl_object_t *sn = ucl_object_lookup(live_blk, "signal_name");
					lv->signal_name = sn
					                ? sanitize_signal_name(ucl_object_tostring(sn))
					                : sanitize_signal_name(opt->name);

					/* parse on blocks from raw file text — UCL merges duplicate keys
					 * so we scan the raw text for:
					 *   on exit N   { ... }
					 *   on output "str" { ... }
					 */
					lv->on_blocks = parse_on_blocks(raw_text, opt->name);

					opt->live = lv;
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
/* shell-escape a string with single quotes for safe use in sh -c.
 * embedded single quotes are replaced with '\'' (end quote, escaped
 * literal quote, reopen quote).  caller must g_free the result.
 */
char *shell_quote(const char *s)
{
	GString *out = g_string_new("'");
	for(; '\0' != *s; s++) {
		if('\'' == *s)
			g_string_append(out, "'\\''");
		else
			g_string_append_c(out, *s);
	}
	g_string_append_c(out, '\'');
	return g_string_free(out, FALSE);
}

/* apply template substitutions.  if for_shell is true, {filename} and
 * {filepath} values are single-quoted for safe use inside sh -c so
 * spaces and special characters in paths do not cause splitting.
 */
char *apply_tpl(const char *tpl, const char *filename,
		const char *filepath, bool for_shell)
{
	char *result = g_strdup(tpl);

	if(strstr(result, "{filename}")) {
		char *val = for_shell ? shell_quote(filename) : g_strdup(filename);
		char **parts = g_strsplit(result, "{filename}", -1);
		g_free(result);
		result = g_strjoinv(val, parts);
		g_strfreev(parts);
		g_free(val);
	}

	if(strstr(result, "{filepath}")) {
		char *val = for_shell ? shell_quote(filepath) : g_strdup(filepath);
		char **parts = g_strsplit(result, "{filepath}", -1);
		g_free(result);
		result = g_strjoinv(val, parts);
		g_strfreev(parts);
		g_free(val);
	}

	return result;
}

/* hard cap for recursive directory traversal — prevents stack overflow
 * from depth = -1 on deep or cyclic directory trees.
 */
#define MAX_GLOB_DEPTH 64

/* recursively expand dir_path, matching filenames against pat, up to max_depth.
 * depth_left == -1 means unlimited (capped internally to MAX_GLOB_DEPTH).
 * appends matched options to *opts.
 * if hierarchy is true, subdirectory entries are skipped (caller handles them).
 */
static void expand_dir(const char *dir_path, const char *pat,
		       const struct populate *pop, int depth_left,
		       GSList **opts)
{
	/* cap unlimited depth to MAX_GLOB_DEPTH */
	if(-1 == depth_left)
		depth_left = MAX_GLOB_DEPTH;
	GError *err = NULL;
	GDir   *dir = g_dir_open(dir_path, 0, &err);

	if(!dir) {
		struct option *e = malloc(sizeof(struct option));
		if(e) {
			char buf[512];
			snprintf(buf, sizeof(buf), "[!] watch error: %s",
				 err ? err->message : dir_path);
			e->name         = strdup(buf);
			e->commands = NULL;
			e->live         = NULL;
			e->subopts          = NULL;
			e->subopts_expanded = false;
			e->order            = -1;
			*opts = g_slist_prepend(*opts, e);
		}
		if(err) g_error_free(err);
		return;
	}

	const char *fname;
	for(; NULL != (fname = g_dir_read_name(dir)); ) {
		char *filepath = g_strconcat(dir_path, G_DIR_SEPARATOR_S, fname, NULL);
		bool  is_dir   = g_file_test(filepath, G_FILE_TEST_IS_DIR);

		/* skip symlinked directories to prevent infinite cycles */
		if(is_dir && g_file_test(filepath, G_FILE_TEST_IS_SYMLINK)) {
			g_free(filepath);
			continue;
		}

		if(is_dir) {
			bool can_recurse = 0 != depth_left;
			if(can_recurse) {
				if(pop->hierarchy) {
					/* hierarchy mode — subdirectory becomes a submenu */
					GSList *sub = NULL;
					expand_dir(filepath, pat, pop,
						   0 < depth_left ? depth_left - 1 : -1,
						   &sub);
					if(sub) {
						struct option *dir_opt = malloc(sizeof(struct option));
						if(dir_opt) {
							dir_opt->name             = strdup(fname);
							dir_opt->commands         = NULL;
							dir_opt->live             = NULL;
							dir_opt->subopts          = sub;
							dir_opt->subopts_expanded = pop->hierarchy_expanded;
							dir_opt->order            = -1;
							*opts = g_slist_prepend(*opts, dir_opt);
						}
					}
				} else {
					/* flat mode — recurse into same list */
					expand_dir(filepath, pat, pop,
						   0 < depth_left ? depth_left - 1 : -1,
						   opts);
				}
			}
			g_free(filepath);
			continue;
		}

		if(!g_pattern_match_simple(pat, fname)) {
			g_free(filepath);
			continue;
		}

		char *label = apply_tpl(pop->label_tpl, fname, filepath, false);
		char *cmd   = apply_tpl(pop->command_tpl, fname, filepath, true);

		struct option *opt = malloc(sizeof(struct option));
		if(opt) {
			opt->name         = strdup(label);
			opt->commands = NULL;
			if(cmd && '\0' != cmd[0]) {
				char **av = g_new(char *, 4);
				av[0] = g_strdup("sh");
				av[1] = g_strdup("-c");
				av[2] = g_strdup(cmd);
				av[3] = NULL;
				opt->commands = g_slist_append(NULL, av);
			}
			opt->live             = NULL;
			opt->subopts          = NULL;
			opt->subopts_expanded = false;
			opt->order            = -1;
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

/* GFileMonitor callback — re-expands the section's populate source on change
 * and emits ss_lib signals for any matching on watch-* blocks.
 */
static void on_glob_changed(GFileMonitor *mon, GFile *file, GFile *other,
			    GFileMonitorEvent event, gpointer user_data)
{
	(void)mon; (void)other;

	struct monitor_ctx *ctx = (struct monitor_ctx *)user_data;

	if(ctx->config->reload_gen != ctx->gen)
		return;

	/* re-expand section options */
	g_slist_free_full(ctx->sec->options, option_dalloc);
	ctx->sec->options = NULL;

	for(GSList *pl = ctx->sec->populates; pl; pl = pl->next) {
		struct populate *pop = (struct populate *)pl->data;
		if(0 == strcmp(pop->from, "glob"))
			ctx->sec->options = g_slist_concat(ctx->sec->options,
							   expand_glob(pop));
	}

	/* map GFileMonitorEvent to on_watch_kind and emit signals */
	on_watch_kind kind;
	bool has_kind = true;
	switch(event) {
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
		kind = ON_WATCH_CHANGE;
		break;
	case G_FILE_MONITOR_EVENT_CREATED:
		kind = ON_WATCH_CREATE;
		break;
	case G_FILE_MONITOR_EVENT_DELETED:
		kind = ON_WATCH_DELETE;
		break;
	default:
		has_kind = false;
		break;
	}

	if(has_kind && ctx->sec->on_watch_blocks) {
		char *filepath = g_file_get_path(file);
		if(filepath) {
			for(GSList *wl = ctx->sec->on_watch_blocks; wl; wl = wl->next) {
				struct on_watch_block *wb = (struct on_watch_block *)wl->data;
				if(wb->kind == kind && wb->signal_name)
					ss_emit_string(wb->signal_name, filepath);
			}
			g_free(filepath);
		}
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
	/* cap unlimited depth */
	if(-1 == depth_left)
		depth_left = MAX_GLOB_DEPTH;

	GFile  *gfile = g_file_new_for_path(dir_path);
	GError *err   = NULL;

	GFileMonitor *mon = g_file_monitor_directory(gfile,
						      G_FILE_MONITOR_NONE,
						      NULL, &err);
	g_object_unref(gfile);

	if(!mon) {
		fprintf(stderr, "gensystray: file watch failed: %s\n", err ? err->message : dir_path);
		if(err) g_error_free(err);
		return;
	}

	struct monitor_ctx *ctx = malloc(sizeof(struct monitor_ctx));
	if(ctx) {
		ctx->sec    = sec;
		ctx->config = config;
		ctx->pop    = pop;
		ctx->gen    = config->reload_gen;
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
		if(g_file_test(subpath, G_FILE_TEST_IS_DIR)
		   && !g_file_test(subpath, G_FILE_TEST_IS_SYMLINK))
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

		static const char *known_populate[] = {
			"from", "pattern", "watch", "depth", "hierarchy", "item", NULL
		};
		warn_unknown_keys(cur, known_populate, "populate");

		const ucl_object_t *from_obj = ucl_object_lookup(cur, "from");
		if(!from_obj)
			continue;

		struct populate *pop = malloc(sizeof(struct populate));
		if(!pop)
			continue;

		pop->from = strdup(ucl_tostring_safe(from_obj, ""));

		const ucl_object_t *pat = ucl_object_lookup(cur, "pattern");
		pop->pattern = strdup(pat ? ucl_tostring_safe(pat, "") : "");

		const ucl_object_t *w = ucl_object_lookup(cur, "watch");
		pop->watch = w && ucl_object_toboolean(w);

		const ucl_object_t *d = ucl_object_lookup(cur, "depth");
		pop->depth = d ? (int)ucl_object_toint(d) : 0;

		const ucl_object_t *h = ucl_object_lookup(cur, "hierarchy");
		pop->hierarchy          = h && ucl_object_toboolean(h);
		pop->hierarchy_expanded = false; /* set by parse_sections after reading section fields */

		const ucl_object_t *item = ucl_object_lookup(cur, "item");
		if(item) {
			static const char *known_pop_item[] = {
				"label", "command", NULL
			};
			warn_unknown_keys(item, known_pop_item, "populate > item");

			const ucl_object_t *lbl = ucl_object_lookup(item, "label");
			const ucl_object_t *cmd = ucl_object_lookup(item, "command");
			pop->label_tpl = strdup(lbl ? ucl_object_tostring(lbl) : "{filename}");

			if(cmd && UCL_ARRAY == ucl_object_type(cmd)) {
				/* array command — join elements with spaces for the template.
				 * expand_dir wraps the result in sh -c so this works for
				 * commands like ["ghostwriter", "{filepath}"]
				 */
				GString *joined = g_string_new(NULL);
				ucl_object_iter_t ait = NULL;
				const ucl_object_t *ae;
				for(; NULL != (ae = ucl_iterate_object(cmd, &ait, true)); ) {
					if(0 < joined->len)
						g_string_append_c(joined, ' ');
					const char *s = ucl_object_tostring(ae);
					if(s)
						g_string_append(joined, s);
				}
				pop->command_tpl = g_string_free(joined, FALSE);
			} else {
				pop->command_tpl = strdup(cmd ? ucl_object_tostring(cmd) : "");
			}
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
static GSList *parse_sections(const ucl_object_t *scope, struct config *config,
				  const char *raw_text) {
	GSList *ordered = NULL;
	GSList *unordered = NULL;

	/* anonymous section for top-level items */
	GSList *flat_opts = parse_options(scope, 0, raw_text);
	if(flat_opts) {
		struct section *anon = malloc(sizeof(struct section));
		if(anon) {
			anon->label           = NULL;
			anon->options         = flat_opts;
			anon->populates       = NULL;
			anon->monitors        = NULL;
			anon->on_watch_blocks = NULL;
			anon->on_emit_blocks  = NULL;
			anon->order           = -1;
			anon->expanded        = false;
			anon->show_label      = true;
			anon->separators      = SEPARATORS_BOTH;
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

				static const char *known_section[] = {
					"item", "populate", "expanded", "hierarchy_expanded",
					"show_label", "separators", "order", NULL
				};
				char sec_desc[128];
				snprintf(sec_desc, sizeof(sec_desc), "section '%s'", sec->label);
				warn_unknown_keys(sec_obj, known_section, sec_desc);

				sec->options  = parse_options(sec_obj, 1, raw_text);
				sec->monitors = NULL;

				/* parse section display fields first so hierarchy_expanded
				 * is known before expand_glob runs */
				const ucl_object_t *exp = ucl_object_lookup(sec_obj, "expanded");
				sec->expanded = exp && ucl_object_toboolean(exp);

				const ucl_object_t *hexe = ucl_object_lookup(sec_obj, "hierarchy_expanded");
				sec->hierarchy_expanded = hexe && ucl_object_toboolean(hexe);

				const ucl_object_t *sl = ucl_object_lookup(sec_obj, "show_label");
				sec->show_label = !sl || ucl_object_toboolean(sl);

				sec->populates = parse_populates(sec_obj);
				for(GSList *pl = sec->populates; pl; pl = pl->next) {
					struct populate *pop = (struct populate *)pl->data;
					/* propagate section-level hierarchy_expanded to populate */
					pop->hierarchy_expanded = sec->hierarchy_expanded;
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

				/* parse on watch-* and on emit blocks from raw text */
				sec->on_watch_blocks = parse_on_watch_blocks(raw_text, sec->label);
				sec->on_emit_blocks  = parse_on_emit_blocks(raw_text, sec->label);

				/* validate: on watch-* blocks require at least one populate
				 * with watch = true, otherwise the events will never fire
				 */
				if(sec->on_watch_blocks) {
					bool has_watch = false;
					for(GSList *pl = sec->populates; pl; pl = pl->next) {
						struct populate *pop = (struct populate *)pl->data;
						if(pop->watch) {
							has_watch = true;
							break;
						}
					}
					if(!has_watch) {
						fprintf(stderr, "gensystray: section '%s': "
						        "'on watch-*' blocks require a populate "
						        "with watch = true\n", sec->label);
						g_slist_free_full(sec->on_watch_blocks,
						                  on_watch_block_dalloc);
						sec->on_watch_blocks = NULL;
					}
				}

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
				  const ucl_object_t *scope, const char *raw_text) {
	struct config *config = malloc(sizeof(struct config));
	if(!config) {
		fprintf(stderr, "gensystray: out of memory\n");
		return NULL;
	}

	config->config_path = strdup(config_path);
	config->name        = name ? strdup(name) : NULL;
	config->icon_path         = NULL;
	config->icon_path_current = NULL;
	config->error_icon_path   = NULL;
	config->tooltip           = NULL;
	config->max_emit_depth    = 0;
	config->sections    = NULL;
	config->tray_icon       = NULL;
	config->menu            = NULL;
	config->main_timer_id = 0;
	config->main_tick_ctx = NULL;
	config->reload_gen    = 0;

	const ucl_object_t *tray = ucl_object_lookup(scope, "tray");
	if(tray) {
		static const char *known_tray[] = {
			"icon", "error_icon", "tooltip", "max_emit_depth", NULL
		};
		char tray_desc[128];
		snprintf(tray_desc, sizeof(tray_desc), "tray%s%s%s",
		         name ? " (instance '" : "", name ? name : "",
		         name ? "')" : "");
		warn_unknown_keys(tray, known_tray, tray_desc);

		const ucl_object_t *icon = ucl_object_lookup(tray, "icon");
		if(icon) {
			const char *icon_str = ucl_object_tostring(icon);
			if(icon_str && '\0' != icon_str[0]) {
				config->icon_path         = strdup(icon_str);
				config->icon_path_current = strdup(icon_str);
			} else {
				if(name)
					fprintf(stderr, "gensystray: instance '%s': tray.icon is empty\n", name);
				else
					fprintf(stderr, "gensystray: tray.icon is empty\n");
			}
		}

		const ucl_object_t *err_icon = ucl_object_lookup(tray, "error_icon");
		if(err_icon) {
			const char *s = ucl_object_tostring(err_icon);
			if(s && '\0' != s[0])
				config->error_icon_path = strdup(s);
		}

		const ucl_object_t *tooltip = ucl_object_lookup(tray, "tooltip");
		if(tooltip)
			config->tooltip = strdup(ucl_tostring_safe(tooltip, "GenSysTray"));

		const ucl_object_t *med = ucl_object_lookup(tray, "max_emit_depth");
		if(med) {
			int v = (int)ucl_object_toint(med);
			if(0 < v)
				config->max_emit_depth = v;
			else
				fprintf(stderr, "gensystray: tray.max_emit_depth must be a positive integer\n");
		}
	} else if(name) {
		fprintf(stderr, "gensystray: instance '%s': missing 'tray' block\n", name);
	}

	config->sections = parse_sections(scope, config, raw_text);

	return config;
}

GSList *load_config(const char *config_path) {
	if(!config_path) {
		fprintf(stderr, "gensystray: no config path provided\n");
		return NULL;
	}

	/* read raw file text — needed both for the on-block mini-parser and to
	 * produce a sanitised copy for UCL.  "on exit N { }" and
	 * "on output "str" { }" are not valid UCL syntax; feeding them verbatim
	 * causes UCL to misparse the live block and leak phantom items into the
	 * menu.  We blank those lines before handing the text to UCL while
	 * keeping raw_text intact for parse_on_blocks.
	 */
	char *raw_text = NULL;
	{
		FILE *f = fopen(config_path, "r");
		if(f) {
			fseek(f, 0, SEEK_END);
			long sz = ftell(f);
			rewind(f);
			raw_text = malloc((size_t)sz + 1);
			if(raw_text) {
				fread(raw_text, 1, (size_t)sz, f);
				raw_text[sz] = '\0';
			}
			fclose(f);
		}
	}

	/* build a sanitised copy for UCL: blank out "on exit ..." and
	 * "on output ..." lines so UCL never sees them.  raw_text is kept
	 * intact for parse_on_blocks which handles those lines itself.
	 * we must blank the entire on block including its braced body,
	 * whether single-line or multi-line, to keep brace balance for UCL.
	 */
	char *ucl_text = raw_text ? strdup(raw_text) : NULL;
	if(ucl_text) {
		char *q = ucl_text;
		for(; '\0' != *q; ) {
			/* skip leading whitespace */
			for(; ' ' == *q || '\t' == *q; q++) {}
			bool is_on = (0 == strncmp(q, "on exit",   7) && (' ' == q[7]  || '\t' == q[7]))
			          || (0 == strncmp(q, "on output", 9) && (' ' == q[9]  || '\t' == q[9] || '"' == q[9]))
			          || (0 == strncmp(q, "on watch-change", 15) && (' ' == q[15] || '\t' == q[15] || '{' == q[15]))
			          || (0 == strncmp(q, "on watch-create", 15) && (' ' == q[15] || '\t' == q[15] || '{' == q[15]))
			          || (0 == strncmp(q, "on watch-delete", 15) && (' ' == q[15] || '\t' == q[15] || '{' == q[15]))
			          || (0 == strncmp(q, "on emit", 7) && (' ' == q[7] || '\t' == q[7] || '"' == q[7]));
			if(is_on) {
				/* blank until we find '{', then blank the whole braced body */
				for(; '\0' != *q && '{' != *q && '\n' != *q; q++)
					*q = ' ';
				if('{' == *q) {
					int depth = 1;
					*q++ = ' ';
					for(; '\0' != *q && 0 < depth; q++) {
						if('{' == *q)      depth++;
						else if('}' == *q) depth--;
						if('\n' != *q)
							*q = ' ';
					}
				}
			} else {
				for(; '\0' != *q && '\n' != *q; q++) {}
			}
			if('\n' == *q) q++;
		}
	}

	struct ucl_parser *parser = ucl_parser_new(0);
	if(!parser) {
		fprintf(stderr, "gensystray: internal error: could not create parser\n");
		free(ucl_text);
		free(raw_text);
		return NULL;
	}

	bool parse_ok = ucl_text
	              ? ucl_parser_add_string(parser, ucl_text, 0)
	              : ucl_parser_add_file(parser, config_path);
	free(ucl_text);

	if(!parse_ok) {
		fprintf(stderr, "gensystray: error while parsing %s: %s\n",
		        config_path, ucl_parser_get_error(parser));
		ucl_parser_free(parser);
		free(raw_text);
		return NULL;
	}

	const ucl_object_t *root = ucl_parser_get_object(parser);
	if(!root) {
		fprintf(stderr, "gensystray: config file is empty: %s\n", config_path);
		ucl_parser_free(parser);
		free(raw_text);
		return NULL;
	}

	GSList *configs = NULL;

	// single instance: top-level tray block present
	if(ucl_object_lookup(root, "tray")) {
		static const char *known_single[] = {
			"tray", "item", "section", NULL
		};
		warn_unknown_keys(root, known_single, "config (single instance)");

		struct config *config = parse_scope(config_path, NULL, root, raw_text);
		if(config)
			configs = g_slist_append(configs, config);
	} else {
		static const char *known_multi[] = { "instance", NULL };
		warn_unknown_keys(root, known_multi, "config (multi instance)");

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
					static const char *known_inst[] = {
						"tray", "item", "section", NULL
					};
					char inst_desc[128];
					snprintf(inst_desc, sizeof(inst_desc),
					         "instance '%s'",
					         ucl_object_key(inst) ? ucl_object_key(inst) : "?");
					warn_unknown_keys(inst, known_inst, inst_desc);

					struct config *config = parse_scope(config_path,
									    ucl_object_key(inst),
									    inst, raw_text);
					if(config)
						configs = g_slist_append(configs, config);
				}
			}
		}
		ucl_object_iterate_free(it);
	}

	free(raw_text);

	if(!configs)
		fprintf(stderr, "gensystray: %s: no 'tray' block or 'instance' blocks found\n",
		        config_path);

	ucl_object_unref((ucl_object_t *)root);
	ucl_parser_free(parser);

	return configs;
}

void free_config_data(struct config *config) {
	if(!config)
		return;

	free(config->config_path);
	free(config->name);
	free(config->icon_path);
	free(config->icon_path_current);
	free(config->error_icon_path);
	free(config->tooltip);

	g_slist_free_full(config->sections, section_dalloc);

	free(config);
}

void free_configs(GSList *configs) {
	g_slist_free_full(configs, (GDestroyNotify)free_config_data);
}
