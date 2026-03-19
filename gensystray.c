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


/* app state — used by popdown_all_menus, check_all_exited, and the
 * config reload policy to coordinate across instances without passing
 * state through signal callbacks
 */
static GSList     *all_configs      = NULL;
static char       *app_cfg_path     = NULL;
static const char *app_instance     = NULL;

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
	struct option  *opt;
	GSubprocess    *proc;
	struct config  *config;
	guint           gen;    /* reload_gen at dispatch time */
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
	bool stale = ctx->config->reload_gen != ctx->gen;
	g_free(ctx);

	if(stale) {
		GError *gerr = NULL;
		g_subprocess_communicate_finish(proc, result, NULL, NULL, &gerr);
		if(gerr) g_error_free(gerr);
		g_object_unref(proc);
		return;
	}

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
	ctx->opt    = opt;
	ctx->proc   = proc;
	ctx->config = (struct config *)opt->live->owner;
	ctx->gen    = ctx->config ? ctx->config->reload_gen : 0;

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

/* context for the main tick — holds the GCD interval and the list of opts */
struct main_tick_ctx {
	guint   interval_ms;
	GSList *opts;
};

/* main tick callback — fires at GCD interval, each item tracks its own
 * countdown. independent items use their own timers and are not in this list.
 */
static gboolean main_tick(gpointer user_data) {
	struct main_tick_ctx *ctx = (struct main_tick_ctx *)user_data;
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
 * all other live items share one main tick at GCD of their refresh intervals.
 */
static void start_live_timers(struct config *config) {
	GSList *shared_opts = NULL;
	guint   shared_ms   = 0;

	/* set ss_lib namespace to instance name for signal scoping */
	ss_set_namespace(config->name ? config->name : "default");

	for(GSList *sl = config->sections; sl; sl = sl->next) {
		struct section *sec = (struct section *)sl->data;
		for(GSList *ol = sec->options; ol; ol = ol->next) {
			struct option *opt = (struct option *)ol->data;
			if(!opt->live)
				continue;

			ss_signal_register(opt->live->signal_name);
			opt->live->owner = config;

			live_tick(opt);   /* initial value before first interval */

			if(opt->live->independent) {
				opt->live->timer_id = g_timeout_add(opt->live->refresh_ms, live_tick, opt);
				continue;
			}

			shared_opts = g_slist_prepend(shared_opts, opt);
			shared_ms   = 0 == shared_ms ? opt->live->refresh_ms
			                             : gcd(shared_ms, opt->live->refresh_ms);
		}
	}

	if(!shared_opts)
		return;

	struct main_tick_ctx *ctx = malloc(sizeof(struct main_tick_ctx));
	if(!ctx) {
		g_slist_free(shared_opts);
		return;
	}
	ctx->interval_ms = shared_ms;
	ctx->opts        = shared_opts;

	config->main_tick_ctx = ctx;
	config->main_timer_id = g_timeout_add(shared_ms, main_tick, ctx);
}

/* stop all live timers for a config: main tick and independent timers.
 * frees main_tick_ctx and its opt list. safe to call if no timers are running.
 */
static void stop_live_timers(struct config *config) {
	/* remove main tick timer */
	if(0 != config->main_timer_id) {
		g_source_remove(config->main_timer_id);
		config->main_timer_id = 0;
	}

	/* free main_tick_ctx and its opt list */
	if(config->main_tick_ctx) {
		struct main_tick_ctx *ctx = (struct main_tick_ctx *)config->main_tick_ctx;
		g_slist_free(ctx->opts);
		free(ctx);
		config->main_tick_ctx = NULL;
	}

	/* remove independent timers and unregister ss_lib signals */
	for(GSList *sl = config->sections; sl; sl = sl->next) {
		struct section *sec = (struct section *)sl->data;
		for(GSList *ol = sec->options; ol; ol = ol->next) {
			struct option *opt = (struct option *)ol->data;
			if(!opt->live)
				continue;
			if(0 != opt->live->timer_id) {
				g_source_remove(opt->live->timer_id);
				opt->live->timer_id = 0;
			}
			ss_signal_unregister(opt->live->signal_name);
		}
	}
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

/* full teardown for a live config instance: stops timers, releases the tray
 * icon GObject, then frees all parsed data. safe to call on any initialised
 * config; free_config_data covers configs that never had runtime state.
 */
static void teardown_config(struct config *config) {
	stop_live_timers(config);
	if(config->tray_icon) {
		g_object_unref(config->tray_icon);
		config->tray_icon = NULL;
	}
	free_config_data(config);
}

/* swap reloadable data fields from src into dst.
 * src's pointers are replaced with dst's old values so freeing src
 * cleans up the old data.
 */
static void swap_config_data(struct config *dst, struct config *src) {
	GSList *old_sections  = dst->sections;
	char   *old_icon_path = dst->icon_path;
	char   *old_tooltip   = dst->tooltip;

	dst->sections  = src->sections;
	dst->icon_path = src->icon_path;
	dst->tooltip   = src->tooltip;

	src->sections  = old_sections;
	src->icon_path = old_icon_path;
	src->tooltip   = old_tooltip;
}

/* config file change callback — reload policy lives here in main.
 * re-parses the config, filters to the active instance, stops timers,
 * swaps data, and restarts timers for each running instance.
 */
static void on_cfg_changed(GFileMonitor *monitor, GFile *file,
			   GFile *other_file, GFileMonitorEvent event_type,
			   gpointer user_data)
{
	(void)monitor; (void)file; (void)other_file;
	(void)event_type; (void)user_data;

	/* close any open menu before swapping data — menu widgets
	 * reference section/option pointers that are about to be freed
	 */
	popdown_all_menus();
	for(GSList *l = all_configs; l; l = l->next)
		disconnect_live_slots((struct config *)l->data);

	GSList *new_list = load_config(app_cfg_path);
	if(!new_list) {
		fprintf(stderr, "gensystray: config reload failed, keeping current config\n");
		return;
	}

	/* match running instances by name and swap their data */
	for(GSList *ol = all_configs; ol; ol = ol->next) {
		struct config *old = (struct config *)ol->data;

		struct config *matched = NULL;
		for(GSList *nl = new_list; nl; nl = nl->next) {
			struct config *c = (struct config *)nl->data;
			if(NULL == old->name && NULL == c->name) {
				matched = c;
				break;
			}
			if(old->name && c->name && 0 == strcmp(old->name, c->name)) {
				matched = c;
				break;
			}
		}

		if(!matched)
			continue;

		stop_live_timers(old);
		old->reload_gen++;
		swap_config_data(old, matched);
		start_live_timers(old);
	}

	free_configs(new_list);
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

/* size-changed callback for file-path icons: reload pixbuf at the exact size
 * the tray requests so icons display at 22x22, 24x24, etc. without forced
 * scaling to 16x16.  user_data is the expanded file path (strdup'd).
 */
static gboolean on_icon_size_changed(GtkStatusIcon *icon, gint size,
				     gpointer user_data)
{
	const char *path = (const char *)user_data;
	GdkPixbuf  *pb   = gdk_pixbuf_new_from_file_at_size(path, size, size, NULL);
	if(pb) {
		gtk_status_icon_set_from_pixbuf(icon, pb);
		g_object_unref(pb);
		return TRUE;  /* handled — suppress GTK's default scaling */
	}
	return FALSE;
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
		/* absolute, relative, or ~ path → load as file.
		 * connect size-changed so the pixbuf is reloaded at the exact
		 * size the tray requests (22, 24, 32 …) rather than 16x16.
		 */
		char *path = config->icon_path;
		char *expanded = NULL;
		if('~' == path[0]) {
			expanded = g_strconcat(g_get_home_dir(), path + 1, NULL);
			path = expanded;
		}
		icon = gtk_status_icon_new_from_file(path);
		/* pass a strdup of the resolved path; freed when icon is destroyed */
		g_signal_connect_data(G_OBJECT(icon), "size-changed",
				      G_CALLBACK(on_icon_size_changed),
				      strdup(path),
				      (GClosureNotify)free, 0);
		g_free(expanded);
	} else {
		/* no path separator → treat as XDG theme icon name;
		 * GTK handles size negotiation automatically for theme icons */
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
	for(int i = 1; i < argc; i++) {
		if(0 == strcmp(argv[i], "--instance") && i + 1 < argc) {
			app_instance = argv[i + 1];
			break;
		}
	}

	app_cfg_path = get_config_path();
	if(!app_cfg_path) {
		fprintf(stderr, "gensystray: could not determine config file path\n");
		exit(EXIT_FAILURE);
	}

	all_configs = load_config(app_cfg_path);
	if(!all_configs) {
		fprintf(stderr, "gensystray: could not load config file\n");
		free(app_cfg_path);
		exit(EXIT_FAILURE);
	}

	all_configs = filter_instance(all_configs, app_instance);

	GFileMonitor *cfg_monitor = monitor_config(app_cfg_path, on_cfg_changed, NULL);

	for(GSList *l = all_configs; l; l = l->next)
		start_live_timers((struct config *)l->data);

	g_slist_foreach(all_configs, (GFunc)init_tray, NULL);

	gtk_main();

	g_slist_foreach(all_configs, (GFunc)teardown_config, NULL);
	g_slist_free(all_configs);
	free(app_cfg_path);
	g_object_unref(cfg_monitor);
	ss_cleanup();

	return 0;
}
