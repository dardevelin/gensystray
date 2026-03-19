/*
 * gensystray_config_parser.h
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

#ifndef _GENSYSTRAY_CFG_PARSER_H
#define _GENSYSTRAY_CFG_PARSER_H

#include <stdio.h>
#include <stdbool.h>
#include <glib.h>
#include "deps/ss_lib/include/ss_lib.h"

typedef enum {
	SEPARATORS_BOTH,   /* true — separator above and below (default) */
	SEPARATORS_TOP,    /* separator above only */
	SEPARATORS_BOTTOM, /* separator below only */
	SEPARATORS_NONE,   /* no separators */
} section_separators;

/* our options from the configuration file consist of name
 * and a command associated with that name.
 * we use this struct to keep this proximity relation clear
 * through out the code
 */
struct option {
	char  *name;
	char **command_argv;  /* click action argv, NULL if none. array = direct exec,
	                       * string/heredoc normalized to ["sh", "-c", cmd, NULL] */
	int    order;         /* -1 = unordered (declaration order) */

	/* live item fields — NULL/0 for static items */
	char   **live_argv;     /* timer command argv, NULL = static. same normalization */
	char    *signal_name;   /* sanitized signal name, derived from name or explicit */
	guint    refresh_ms;    /* polling interval in milliseconds */
	guint    tick_counter;  /* master tick countdown, reset when it fires */
	bool     independent;   /* true = own timer, excluded from master tick */
	char    *live_output;   /* last output from live_cmd */
	guint    timer_id;      /* GLib timer source ID, 0 = not running */
	void             *live_label;   /* GtkWidget *, set while menu is open */
	ss_connection_t   live_conn;    /* ss_lib connection handle, 0 = not connected */
};

/* context passed to GFileMonitor callbacks for glob populate sources.
 * freed automatically via g_object_set_data_full when the monitor is unreffed.
 */
struct monitor_ctx {
	struct section  *sec;
	struct config   *config;
	struct populate *pop;
};

/* a populate block describes a dynamic source for section items.
 * from = "glob": pattern is a glob e.g. ~/notes/\*.md, label_tpl and command_tpl
 * use {filename} and {filepath} substitution tokens.
 * watch = true: a GFileMonitor watches the directory and re-expands on change.
 */
struct populate {
	char  *from;        /* source type: "glob", others TBD */
	char  *pattern;     /* glob pattern, ~ expanded at runtime */
	char  *label_tpl;   /* item label template, e.g. "{filename}" */
	char  *command_tpl; /* item command template, e.g. "nvim {filepath}" */
	bool   watch;       /* true = monitor directory for changes */
	int    depth;       /* 0 = current dir only, N = N levels, -1 = unlimited */
	bool   hierarchy;   /* true = subdirs become submenus, false = flat list */
};

/* a section is a named submenu containing a list of options.
 * label == NULL means an anonymous (flat) section — top-level items.
 * ordering among sections follows the same rules as options:
 * explicit order first, then declaration order.
 * items within a named section are sorted alphabetically by label.
 * items within an anonymous section follow declaration order.
 */
struct section {
	char   *label;          /* NULL = anonymous/flat */
	GSList *options;        /* expanded struct option list */
	GSList *populates;      /* struct populate list for dynamic sources */
	GSList *monitors;       /* GFileMonitor* list for watched sources */
	int     order;          /* -1 = unordered (declaration order) */
	bool    expanded;       /* false = submenu (default), true = inline */
	bool               show_label;  /* true = show name as header (default) */
	section_separators separators;  /* controls top/bottom separator rendering */
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
	char *name;       /* instance name, NULL in single-instance mode */
	char *icon_path;
	char *tooltip;
	GSList *sections; /* ordered list of struct section */
	void *tray_icon;  /* GtkStatusIcon *, kept alive here */
	void *menu;      /* GtkMenu *, currently open menu or NULL */
};

/* allocates, populates and returns a GSList of struct config from config_path.
 * single instance config returns a list of one.
 * multi instance config (instance blocks) returns one entry per instance.
 * returns NULL on failure. caller must free with free_configs.
 */
GSList *load_config(const char *config_path);

/* frees all memory owned by a single config, including the struct itself */
void free_config(struct config *config);

/* frees the full list returned by load_config */
void free_configs(GSList *configs);

#endif
