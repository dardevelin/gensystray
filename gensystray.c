/*
 * gensystray.c
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

/* we use gtk for our tray icon system */
#include <gtk/gtk.h>

/* import our configuration routines  */
#include "gensystray_config_parser.h"

/* import config monitor for live config reloading */
#include "gensystray_config_monitor.h"

/* platform-specific menu dismiss (macOS: NSEvent monitor, Linux: no-op) */
#include "gensystray_platform.h"

/* signal-slot dispatch for live items and IPC */
#include "deps/ss_lib/include/ss_lib.h"


/* global list of all loaded instances — used by popdown_all_menus and
 * check_all_exited to coordinate across instances without passing state
 * through signal callbacks
 */
static GSList *all_configs = NULL;

/* dismiss every open instance menu. called when any tray icon is clicked
 * or when a click outside all GTK windows is detected by the osx monitor
 */
static void popdown_all_menus(void) {
	for(GSList *l = all_configs; l; l = l->next) {
		struct config *c = (struct config *)l->data;
		if(c->menu) {
			gtk_menu_popdown(GTK_MENU(c->menu));
			c->menu = NULL;
		}
	}
}

/* called after each instance exits. if no instances have a tray icon
 * remaining, quit the gtk main loop and let the process exit cleanly
 */
static void check_all_exited(void) {
	for(GSList *l = all_configs; l; l = l->next) {
		struct config *c = (struct config *)l->data;
		if(c->tray_icon)
			return;
	}
	gtk_main_quit();
}

/* spawn all commands in a GSList of char** argv entries */
static void spawn_commands(GSList *commands) {
	for(GSList *l = commands; l; l = l->next) {
		char **argv = (char **)l->data;
		if(argv && argv[0])
			g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
				      NULL, NULL, NULL, NULL);
	}
}

/* valid "activate" signal callback. spawns all commands in the list.
 * user_data is GSList* of char** argv entries.
 */
void delegate_system_call(GtkWidget *widget, gpointer user_data) {
	spawn_commands((GSList *)user_data);
}

/* ss_lib slot — called when a live signal is emitted.
 * updates the GtkLabel widget with the new output string.
 */
static void on_live_signal(const ss_data_t *data, void *user_data) {
	GtkWidget  *label  = (GtkWidget *)user_data;
	const char *output = ss_data_get_string(data);
	if(label && output)
		gtk_label_set_text(GTK_LABEL(label), output);
}

/* context passed to the async update_label callback */
struct live_cb_ctx {
	struct option *opt;
	GSubprocess   *proc;
};

/* async callback — runs on the GTK main loop after update_label_argv exits.
 * trims stdout, matches on blocks, updates the label, emits the signal.
 */
static void on_update_label_done(GObject *source, GAsyncResult *result,
				 gpointer user_data)
{
	struct live_cb_ctx *ctx  = (struct live_cb_ctx *)user_data;
	struct option      *opt  = ctx->opt;
	GSubprocess        *proc = ctx->proc;
	g_free(ctx);

	GBytes *stdout_buf = NULL;
	GBytes *stderr_buf = NULL;
	GError *gerr       = NULL;

	g_subprocess_communicate_finish(proc, result,
					&stdout_buf, &stderr_buf, &gerr);

	if(gerr) {
		g_error_free(gerr);
		if(stdout_buf) g_bytes_unref(stdout_buf);
		if(stderr_buf) g_bytes_unref(stderr_buf);
		g_object_unref(proc);
		return;
	}

	int status = g_subprocess_get_if_exited(proc)
	           ? g_subprocess_get_exit_status(proc)
	           : -1;
	g_object_unref(proc);

	gsize        len = 0;
	const gchar *raw = NULL;
	if(stdout_buf)
		raw = (const gchar *)g_bytes_get_data(stdout_buf, &len);
	if(!raw) {
		raw = "";
		len = 0;
	}

	/* copy stdout so we can mutate (trim newline) */
	char *out = g_strndup(raw, len);
	char *nl  = strchr(out, '\n');
	if(nl)
		*nl = '\0';

	char *trimmed = out;

	/* match on blocks — first match wins */
	struct on_block *matched = NULL;
	for(GSList *ol = opt->live->on_blocks; ol; ol = ol->next) {
		struct on_block *ob = (struct on_block *)ol->data;

		if(ON_EXIT == ob->kind && status == ob->exit_code) {
			matched = ob;
			break;
		}

		if(ON_OUTPUT == ob->kind && ob->output_match && strstr(trimmed, ob->output_match)) {
			matched = ob;
			break;
		}
	}

	/* fire on block command only on state transition */
	if(matched != opt->live->last_matched) {
		opt->live->last_matched = matched;
		if(matched && matched->commands)
			spawn_commands(matched->commands);
	}

	/* determine display label */
	const char *display = matched && matched->label
	                    ? matched->label
	                    : trimmed;

	free(opt->live->live_output);
	opt->live->live_output = strdup(display);

	/* emit — slots (label widgets) receive the update if connected */
	ss_emit_string(opt->live->signal_name, opt->live->live_output);

	g_free(out);
	if(stdout_buf) g_bytes_unref(stdout_buf);
	if(stderr_buf) g_bytes_unref(stderr_buf);
}

/* launch update_label_argv async and fire timed side-effect commands.
 * returns TRUE so g_timeout_add keeps firing.
 */
static gboolean live_tick(gpointer user_data) {
	struct option *opt = (struct option *)user_data;

	/* run timed commands if set (side effects, no label update) */
	if(opt->live->commands)
		spawn_commands(opt->live->commands);

	if(!opt->live->update_label_argv)
		return TRUE;

	GError      *gerr = NULL;
	GSubprocess *proc = g_subprocess_newv(
	                        (const gchar * const *)opt->live->update_label_argv,
	                        G_SUBPROCESS_FLAGS_STDOUT_PIPE |
	                        G_SUBPROCESS_FLAGS_STDERR_SILENCE,
	                        &gerr);

	if(!proc) {
		g_error_free(gerr);
		return TRUE;
	}

	struct live_cb_ctx *ctx = g_new(struct live_cb_ctx, 1);
	ctx->opt  = opt;
	ctx->proc = proc;

	g_subprocess_communicate_async(proc, NULL, NULL,
				       on_update_label_done, ctx);
	return TRUE;
}

/* Euclid's algorithm — used to find the master tick interval.
 * given all live item refresh intervals, their GCD is the fastest interval
 * at which one timer can serve all of them. e.g. 3s and 5s items share a
 * 1s master tick; the 3s item fires every 3rd tick, the 5s every 5th.
 */
static guint gcd(guint a, guint b) {
	for(; 0 != b; ) {
		guint t = b;
		b = a % b;
		a = t;
	}
	return a;
}

/* context for the master tick — holds the GCD interval and the list of opts */
struct master_ctx {
	guint   interval_ms;
	GSList *opts;
};

/* master tick callback — fires at GCD interval, each item tracks its own
 * countdown. independent items use their own timers and are not in this list.
 */
static gboolean master_tick(gpointer user_data) {
	struct master_ctx *ctx = (struct master_ctx *)user_data;
	for(GSList *l = ctx->opts; l; l = l->next) {
		struct option *opt = (struct option *)l->data;
		opt->live->tick_counter += ctx->interval_ms;
		if(opt->live->tick_counter >= opt->live->refresh_ms) {
			opt->live->tick_counter = 0;
			live_tick(opt);
		}
	}
	return TRUE;
}

/* start timers for all live options across all sections of config.
 * independent items get their own g_timeout_add timer.
 * all other live items share one master tick at GCD of their refresh intervals.
 */
static void start_live_timers(struct config *config) {
	GSList *master_opts = NULL;
	guint   master_ms   = 0;

	/* set ss_lib namespace to instance name for signal scoping */
	ss_set_namespace(config->name ? config->name : "default");

	for(GSList *sl = config->sections; sl; sl = sl->next) {
		struct section *sec = (struct section *)sl->data;
		for(GSList *ol = sec->options; ol; ol = ol->next) {
			struct option *opt = (struct option *)ol->data;
			if(!opt->live)
				continue;

			ss_signal_register(opt->live->signal_name);

			live_tick(opt);   /* initial value before first interval */

			if(opt->live->independent) {
				opt->live->timer_id = g_timeout_add(opt->live->refresh_ms, live_tick, opt);
				continue;
			}

			master_opts = g_slist_prepend(master_opts, opt);
			master_ms   = 0 == master_ms ? opt->live->refresh_ms
			                             : gcd(master_ms, opt->live->refresh_ms);
		}
	}

	if(!master_opts)
		return;

	struct master_ctx *ctx = malloc(sizeof(struct master_ctx));
	if(!ctx) {
		g_slist_free(master_opts);
		return;
	}
	ctx->interval_ms = master_ms;
	ctx->opts        = master_opts;

	g_timeout_add(master_ms, master_tick, ctx);
}

/* valid GFunc signature for g_slist_foreach. builds one GtkMenuItem per
 * option and appends it to the menu. separators are detected by the "--"
 * convention on both name and command fields
 */
void gensystray_option_to_item(gpointer data, gpointer param) {
	GtkMenu *menu = (GtkMenu*)param;
	struct option *option = (struct option*)data;
	GtkWidget *menu_item = NULL;

	if('-' == option->name[0] && NULL == option->commands) {
		menu_item = gtk_separator_menu_item_new();
	} else if(option->subopts && !option->subopts_expanded) {
		/* hierarchy submenu — subdirectory as a collapsed nested menu */
		menu_item = gtk_menu_item_new_with_label(option->name);
		GtkWidget *submenu = gtk_menu_new();
		g_slist_foreach(option->subopts, gensystray_option_to_item, submenu);
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), submenu);
	} else if(option->subopts && option->subopts_expanded) {
		/* hierarchy_expanded — render subdir contents flat with a disabled header */
		GtkWidget *header = gtk_menu_item_new_with_label(option->name);
		gtk_widget_set_sensitive(header, FALSE);
		gtk_menu_shell_append((GtkMenuShell *)menu, header);
		g_slist_foreach(option->subopts, gensystray_option_to_item, menu);
		/* skip the normal append at the bottom — already appended header */
		gtk_widget_show_all(GTK_WIDGET(menu));
		return;
	} else if(option->live) {
		/* live item — label widget connected as ss_lib slot while menu is open */
		const char *text = option->live->live_output ? option->live->live_output : option->name;
		GtkWidget  *label = gtk_label_new(text);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
		menu_item = gtk_menu_item_new();
		gtk_container_add(GTK_CONTAINER(menu_item), label);
		option->live->live_label = label;
		ss_connect_ex(option->live->signal_name, on_live_signal, label,
			      SS_PRIORITY_NORMAL, &option->live->live_conn);
		if(option->commands)
			g_signal_connect(G_OBJECT(menu_item), "activate",
					 G_CALLBACK(delegate_system_call),
					 option->commands);
		g_signal_connect_swapped(G_OBJECT(menu_item), "destroy",
					 G_CALLBACK(g_nullify_pointer),
					 &option->live->live_label);
	} else {
		menu_item = gtk_menu_item_new_with_label(option->name);
		g_signal_connect(G_OBJECT(menu_item), "activate",
				 G_CALLBACK(delegate_system_call),
				 option->commands);
	}

	gtk_menu_shell_append((GtkMenuShell*)menu, menu_item);
	gtk_widget_show_all(GTK_WIDGET(menu));
}

/* disconnect all live ss_lib slots for a config when menu closes */
static void disconnect_live_slots(struct config *config) {
	for(GSList *sl = config->sections; sl; sl = sl->next) {
		struct section *sec = (struct section *)sl->data;
		for(GSList *ol = sec->options; ol; ol = ol->next) {
			struct option *opt = (struct option *)ol->data;
			if(!opt->live || 0 == opt->live->live_conn)
				continue;
			ss_disconnect_handle(opt->live->live_conn);
			opt->live->live_conn = 0;
		}
	}
}

/* hides and releases the tray icon for this instance, then checks if all
 * instances have exited. connected to the "exit" menu item per instance
 */
static void destroy_instance(GtkWidget *widget, gpointer user_data) {
	struct config *config = (struct config *)user_data;
	gtk_status_icon_set_visible(GTK_STATUS_ICON(config->tray_icon), FALSE);
	g_object_unref(config->tray_icon);
	config->tray_icon = NULL;
	config->menu = NULL;
	check_all_exited();
}

/* popup-menu signal callback. dismisses all open menus first (handles the
 * case where another instance's menu is open), then builds and shows a new
 * menu for this instance.
 *
 * the deactivate signal is used to:
 *   - clear config->menu so we don't hold a stale pointer
 *   - uninstall the osx global event monitor
 *
 * the osx monitor is installed after popup so any outside click triggers
 * popdown_all_menus, which also covers clicking between instances
 */
void gensystray_on_menu(GtkStatusIcon *icon, guint button,
			guint activate_time, gpointer user_data) {
	struct config *config = (struct config*)user_data;

	popdown_all_menus();

	GtkMenu *menu = (GtkMenu*)gtk_menu_new();
	config->menu = menu;

	for(GSList *sl = config->sections; sl; sl = sl->next) {
		struct section *sec = (struct section *)sl->data;

		if(!sec->label) {
			/* anonymous section — render items flat into the menu */
			g_slist_foreach(sec->options, gensystray_option_to_item, menu);
			continue;
		}

		if(!sec->expanded) {
			/* collapsed section — render as a submenu */
			GtkWidget *sub_item = gtk_menu_item_new_with_label(sec->label);
			GtkMenu   *submenu  = (GtkMenu *)gtk_menu_new();
			g_slist_foreach(sec->options, gensystray_option_to_item, submenu);
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(sub_item), GTK_WIDGET(submenu));
			gtk_menu_shell_append((GtkMenuShell *)menu, sub_item);
			continue;
		}

		/* expanded section — inline with configurable separators */
		bool sep_top    = false;
		bool sep_bottom = false;

		if(sec->separators == SEPARATORS_TOP)    sep_top    = true;
		if(sec->separators == SEPARATORS_BOTTOM) sep_bottom = true;
		if(sec->separators == SEPARATORS_BOTH)   sep_top    = true;
		if(sec->separators == SEPARATORS_BOTH)   sep_bottom = true;

		if(sep_top)
			gtk_menu_shell_append((GtkMenuShell *)menu,
					      gtk_separator_menu_item_new());
		if(sec->show_label) {
			GtkWidget *header = gtk_menu_item_new_with_label(sec->label);
			gtk_widget_set_sensitive(header, FALSE);
			gtk_menu_shell_append((GtkMenuShell *)menu, header);
		}
		g_slist_foreach(sec->options, gensystray_option_to_item, menu);
		if(sep_bottom)
			gtk_menu_shell_append((GtkMenuShell *)menu,
					      gtk_separator_menu_item_new());
	}

	GtkWidget *exit_item = gtk_menu_item_new_with_label("exit");
	g_signal_connect(G_OBJECT(exit_item), "activate",
			 G_CALLBACK(destroy_instance), config);
	gtk_menu_shell_append((GtkMenuShell*)menu, exit_item);

	gtk_widget_show_all(GTK_WIDGET(menu));

	g_signal_connect_swapped(G_OBJECT(menu), "deactivate",
				 G_CALLBACK(g_nullify_pointer), &config->menu);
	g_signal_connect_swapped(G_OBJECT(menu), "deactivate",
				 G_CALLBACK(platform_menu_unwatch), menu);
	g_signal_connect_swapped(G_OBJECT(menu), "deactivate",
				 G_CALLBACK(disconnect_live_slots), config);

	gtk_menu_popup((GtkMenu*)menu, NULL, NULL,
		       gtk_status_icon_position_menu, icon,
		       button, activate_time);

	platform_menu_watch(menu, popdown_all_menus);
}

/* if name is NULL, returns configs unchanged. otherwise finds the named
 * instance, frees the rest, and returns a one-element list. exits on no match.
 */
static GSList *filter_instance(GSList *configs, const char *name) {
	if(!name)
		return configs;

	struct config *match = NULL;
	for(GSList *l = configs; l; l = l->next) {
		struct config *c = (struct config *)l->data;
		if(c->name && strcmp(c->name, name) == 0) {
			match = c;
			break;
		}
	}

	if(!match) {
		fprintf(stderr, "gensystray: instance '%s' not found in config\n", name);
		free_configs(configs);
		exit(EXIT_FAILURE);
	}

	configs = g_slist_remove(configs, match);
	free_configs(configs);
	return g_slist_prepend(NULL, match);
}

/* creates a GtkStatusIcon for the given config instance, sets its icon
 * and tooltip, connects the popup-menu signal, and stores the icon pointer
 * in config->tray_icon to keep it alive
 */
static GtkStatusIcon *init_tray(struct config *config) {
	GtkStatusIcon *icon = NULL;

	if(!config->icon_path) {
		/* no icon specified — fall back to a generic theme icon */
		icon = gtk_status_icon_new_from_icon_name("application-x-executable");
	} else if('/' == config->icon_path[0] || '.' == config->icon_path[0]
	          || '~' == config->icon_path[0]) {
		/* absolute, relative, or ~ path → load as file */
		char *path = config->icon_path;
		char *expanded = NULL;
		if('~' == path[0]) {
			expanded = g_strconcat(g_get_home_dir(), path + 1, NULL);
			path = expanded;
		}
		icon = gtk_status_icon_new_from_file(path);
		g_free(expanded);
	} else {
		/* no path separator → treat as XDG theme icon name */
		icon = gtk_status_icon_new_from_icon_name(config->icon_path);
	}

	if(config->tooltip)
		gtk_status_icon_set_tooltip_text(icon, config->tooltip);

	g_signal_connect(G_OBJECT(icon), "popup-menu",
			 G_CALLBACK(gensystray_on_menu), config);

	gtk_status_icon_set_visible(icon, TRUE);

	config->tray_icon = icon;
	return icon;
}

int main(int argc, char **argv) {
	ss_init();
	gtk_init(&argc, &argv);

	/* parse --instance <name> before gtk consumes argv */
	const char *instance_filter = NULL;
	for(int i = 1; i < argc; i++) {
		if(strcmp(argv[i], "--instance") == 0 && i + 1 < argc) {
			instance_filter = argv[i + 1];
			break;
		}
	}

	char *cfg_path = get_config_path();
	if(!cfg_path) {
		fprintf(stderr, "gensystray: could not determine config file path\n");
		exit(EXIT_FAILURE);
	}

	all_configs = load_config(cfg_path);
	if(!all_configs) {
		fprintf(stderr, "gensystray: could not load config file\n");
		free(cfg_path);
		exit(EXIT_FAILURE);
	}

	all_configs = filter_instance(all_configs, instance_filter);

	for(GSList *l = all_configs; l; l = l->next)
		start_live_timers((struct config *)l->data);

	/* monitor watches the first config's path — all instances share one file */
	GFileMonitor *cfg_monitor = monitor_config(cfg_path, (struct config *)all_configs->data);
	free(cfg_path);

	g_slist_foreach(all_configs, (GFunc)init_tray, NULL);

	gtk_main();

	for(GSList *l = all_configs; l; l = l->next) {
		struct config *c = (struct config *)l->data;
		if(c->tray_icon)
			g_object_unref(c->tray_icon);
	}
	free_configs(all_configs);
	g_object_unref(cfg_monitor);
	ss_cleanup();

	return 0;
}
