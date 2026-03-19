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
typedef enum {
	ON_EXIT,    /* matches on exact exit code */
	ON_OUTPUT,  /* matches if stdout contains a substring */
} on_kind;

/* a single on block inside a live block.
 * kind determines which field is used for matching.
 * label overrides the display label when matched (NULL = raw stdout).
 * commands: list of char** argv, each spawned once on state transition.
 */
struct on_block {
	on_kind  kind;
	int      exit_code;    /* ON_EXIT: exact exit code to match */
	char    *output_match; /* ON_OUTPUT: substring to match in stdout */
	char    *label;        /* display label override, NULL = raw stdout */
	GSList  *commands;     /* list of char** argv, fired on transition */
};

/* live block — timer-driven behavior for an item.
 * update_label_argv: run on timer, stdout becomes the label (NULL = no label update).
 * commands: list of char** argv run on timer as side effects, no label change.
 * on_blocks: ordered list of struct on_block, first match wins each tick.
 * last_matched: tracks previous tick's match for transition detection.
 */
struct live {
	char   **update_label_argv;      /* stdout → label on each tick */
	char   **update_tray_icon_argv;  /* stdout → icon_path_current on each tick */
	GSList  *commands;               /* list of char** argv, timed side effects */
	GSList  *on_blocks;            /* struct on_block list, declaration order */
	struct on_block *last_matched; /* previous tick match, NULL = none */
	char    *signal_name;          /* sanitized signal name for ss_lib */
	guint    refresh_ms;           /* polling interval in milliseconds */
	guint    tick_counter;         /* master tick countdown, reset when fired */
	bool     independent;          /* true = own timer, excluded from master tick */
	char    *live_output;          /* last label output */
	guint    timer_id;             /* GLib timer source ID, 0 = not running */
	void            *live_label;   /* GtkWidget *, set while menu is open */
	ss_connection_t  live_conn;    /* ss_lib connection handle, 0 = not connected */
	void            *owner;        /* struct config * that owns this live item */
};

struct option {
	char       *name;
	GSList     *commands;        /* list of char** argv, click actions, NULL if none */
	int         order;           /* -1 = unordered (declaration order) */
	struct live *live;           /* NULL for static items */
	GSList     *subopts;         /* child options for hierarchy submenus, NULL if leaf */
	bool        subopts_expanded;/* true = render subopts flat/inline, false = nested submenu */
};

/* context passed to GFileMonitor callbacks for glob populate sources.
 * freed automatically via g_object_set_data_full when the monitor is unreffed.
 */
struct monitor_ctx {
	struct section  *sec;
	struct config   *config;
	struct populate *pop;
	guint            gen;  /* config->reload_gen at monitor creation time */
};

/* a populate block describes a dynamic source for section items.
 * from = "glob": pattern is a glob e.g. ~/notes/\*.md, label_tpl and command_tpl
 * use {filename} and {filepath} substitution tokens.
 * watch = true: a GFileMonitor watches the directory and re-expands on change.
 */
struct populate {
	char  *from;               /* source type: "glob", others TBD */
	char  *pattern;            /* glob pattern, ~ expanded at runtime */
	char  *label_tpl;          /* item label template, e.g. "{filename}" */
	char  *command_tpl;        /* item command template, e.g. "nvim {filepath}" */
	bool   watch;              /* true = monitor directory for changes */
	int    depth;              /* 0 = current dir only, N = N levels, -1 = unlimited */
	bool   hierarchy;          /* true = subdirs become submenus, false = flat list */
	bool   hierarchy_expanded; /* true = subdir submenus render flat, false = collapsed (default) */
};

/* a section is a named submenu containing a list of options.
 * label == NULL means an anonymous (flat) section — top-level items.
 * ordering among sections follows the same rules as options:
 * explicit order first, then declaration order.
 * items within a named section are sorted alphabetically by label.
 * items within an anonymous section follow declaration order.
 */
struct section {
	char   *label;               /* NULL = anonymous/flat */
	GSList *options;             /* expanded struct option list */
	GSList *populates;           /* struct populate list for dynamic sources */
	GSList *monitors;            /* GFileMonitor* list for watched sources */
	int     order;               /* -1 = unordered (declaration order) */
	bool    expanded;            /* false = submenu (default), true = inline */
	bool    hierarchy_expanded;  /* true = hierarchy subdirs render flat, false = collapsed submenus */
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
	char *name;                 /* instance name, NULL in single-instance mode */
	char *icon_path;            /* parsed default from config file */
	char *icon_path_current;    /* runtime value, initialized from icon_path, overridable */
	char *tooltip;
	GSList *sections; /* ordered list of struct section */
	void *tray_icon;  /* GtkStatusIcon *, kept alive here */
	void *menu;       /* GtkMenu *, currently open menu or NULL */
	guint  main_timer_id;    /* GLib source ID for master tick, 0 = none */
	void  *main_tick_ctx;    /* struct main_tick_ctx *, owned by live engine */
	guint  reload_gen;       /* incremented on every reload; in-flight callbacks check this */
};

/* allocates, populates and returns a GSList of struct config from config_path.
 * single instance config returns a list of one.
 * multi instance config (instance blocks) returns one entry per instance.
 * returns NULL on failure. caller must free with free_configs.
 */
GSList *load_config(const char *config_path);

/* frees parsed fields owned by the config struct (name, icon_path, tooltip,
 * sections, config_path) and the struct itself.
 * does NOT touch runtime state: tray_icon, main_tick_ctx, main_timer_id.
 * call teardown_config (in main) for configs that have been fully initialised.
 */
void free_config_data(struct config *config);

/* frees the full list returned by load_config — safe for freshly parsed
 * configs that have never had runtime state attached.
 */
void free_configs(GSList *configs);

#endif
