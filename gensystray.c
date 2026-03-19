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

/* show a config error on a running tray icon: swap to the error icon
 * and set the tooltip to the message.  the next successful reload
 * will call apply_tray_update which overwrites both, clearing the error.
 *
 * icon priority: config error_icon_path → bundled gensystray_error.png
 * (next to binary) → theme "dialog-error".
 */
static void show_tray_error(struct config *config, const char *msg) {
	GtkStatusIcon *icon = (GtkStatusIcon *)config->tray_icon;
	if(!icon)
		return;

	gint size = gtk_status_icon_get_size(icon);
	bool icon_set = false;

	/* try user-configured error icon */
	if(config->error_icon_path) {
		const char *path = config->error_icon_path;
		char *expanded = NULL;
		if('~' == path[0]) {
			expanded = g_strconcat(g_get_home_dir(), path + 1, NULL);
			path = expanded;
		}
		GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(path, size, size, NULL);
		if(pb) {
			gtk_status_icon_set_from_pixbuf(icon, pb);
			g_object_unref(pb);
			icon_set = true;
		}
		g_free(expanded);
	}

	/* try bundled error icon next to the binary */
	if(!icon_set) {
		char *self = g_find_program_in_path("gensystray");
		if(self) {
			char *dir = g_path_get_dirname(self);
			char *bundled = g_build_filename(dir, "gensystray_error.png", NULL);
			GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(bundled, size, size, NULL);
			if(pb) {
				gtk_status_icon_set_from_pixbuf(icon, pb);
				g_object_unref(pb);
				icon_set = true;
			}
			g_free(bundled);
			g_free(dir);
			g_free(self);
		}
	}

	/* last resort: theme icon */
	if(!icon_set)
		gtk_status_icon_set_from_icon_name(icon, "dialog-error");

	gtk_status_icon_set_tooltip_text(icon, msg);
}

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

/* emit chain depth guard — prevents infinite recursion from circular
 * emit chains (A → B → A).  default 16 levels is enough for legitimate
 * chains while catching accidental cycles before the stack overflows.
 * overridable per config via tray { max_emit_depth = N }.
 */
static int emit_depth     = 0;
static int max_emit_depth = 16;

/* fire an emit signal. handles the "global." prefix by switching
 * to the __global namespace for cross-instance signals.
 */
static void fire_emit(const struct emit *em) {
	if(!em || !em->signal)
		return;

	if(emit_depth >= max_emit_depth) {
		fprintf(stderr, "gensystray: emit chain depth exceeded %d "
		        "(possible cycle at signal '%s')\n",
		        max_emit_depth, em->signal);
		return;
	}

	emit_depth++;

	if(0 == strncmp(em->signal, "global.", 7)) {
		ss_data_t data = { .type = SS_TYPE_VOID };
		if(em->value) {
			data.type = SS_TYPE_STRING;
			data.value.s_val = em->value;
		}
		ss_emit_namespaced("__global", em->signal, &data);
	} else {
		if(em->value)
			ss_emit_string(em->signal, em->value);
		else
			ss_emit_void(em->signal);
	}

	emit_depth--;
}

/* context for delegate_system_call_and_emit — carries both commands and emit */
struct click_ctx {
	GSList      *commands;
	struct emit *emit;
};

/* valid "activate" signal callback. spawns all commands in the list.
 * user_data is GSList* of char** argv entries.
 */
void delegate_system_call(GtkWidget *widget, gpointer user_data) {
	spawn_commands((GSList *)user_data);
}

/* activate callback that also fires an emit signal after spawning commands.
 * user_data is struct click_ctx*.
 */
static void delegate_system_call_and_emit(GtkWidget *widget, gpointer user_data) {
	struct click_ctx *ctx = (struct click_ctx *)user_data;
	if(ctx->commands)
		spawn_commands(ctx->commands);
	fire_emit(ctx->emit);
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

/* apply icon_path_current and tooltip to the existing GtkStatusIcon.
 * size-changed stays connected to config so it always reads icon_path_current.
 */
static void apply_tray_update(struct config *config) {
	GtkStatusIcon *icon = (GtkStatusIcon *)config->tray_icon;
	if(!icon)
		return;

	const char *path = config->icon_path_current;

	if(!path) {
		gtk_status_icon_set_from_icon_name(icon, "application-x-executable");
	} else if('/' == path[0] || '.' == path[0] || '~' == path[0]) {
		char *expanded = NULL;
		if('~' == path[0]) {
			expanded = g_strconcat(g_get_home_dir(), path + 1, NULL);
			path = expanded;
		}
		gint size = gtk_status_icon_get_size(icon);
		GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(path, size, size, NULL);
		if(pb) {
			gtk_status_icon_set_from_pixbuf(icon, pb);
			g_object_unref(pb);
		} else {
			char *msg = g_strdup_printf("config error: tray icon not found: %s", path);
			fprintf(stderr, "gensystray: tray icon file not found: %s\n", path);
			g_free(expanded);
			show_tray_error(config, msg);
			g_free(msg);
			return;
		}
		g_free(expanded);
	} else {
		gtk_status_icon_set_from_icon_name(icon, path);
	}

	if(config->tooltip)
		gtk_status_icon_set_tooltip_text(icon, config->tooltip);
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

	/* fire on block command and emit only on state transition */
	if(matched != opt->live->last_matched) {
		opt->live->last_matched = matched;
		if(matched) {
			if(matched->commands)
				spawn_commands(matched->commands);
			fire_emit(matched->emit);
		}
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

/* async callback — runs after update_tray_icon_argv exits.
 * trims stdout and writes it to icon_path_current, then applies the update.
 */
static void on_update_tray_icon_done(GObject *source, GAsyncResult *result,
				     gpointer user_data)
{
	struct live_cb_ctx *ctx  = (struct live_cb_ctx *)user_data;
	GSubprocess        *proc = ctx->proc;
	struct config      *config = ctx->config;
	bool stale = config->reload_gen != ctx->gen;
	g_free(ctx);

	GBytes *stdout_buf = NULL;
	GError *gerr       = NULL;
	g_subprocess_communicate_finish(proc, result, &stdout_buf, NULL, &gerr);
	if(gerr) g_error_free(gerr);
	g_object_unref(proc);

	if(stale) {
		if(stdout_buf) g_bytes_unref(stdout_buf);
		return;
	}

	if(!stdout_buf) return;

	gsize        len = 0;
	const gchar *raw = (const gchar *)g_bytes_get_data(stdout_buf, &len);
	if(raw && 0 < len) {
		char *out = g_strndup(raw, len);
		char *nl  = strchr(out, '\n');
		if(nl) *nl = '\0';

		if('\0' != out[0]) {
			free(config->icon_path_current);
			config->icon_path_current = strdup(out);
			apply_tray_update(config);
		}
		g_free(out);
	}
	g_bytes_unref(stdout_buf);
}

/* launch update_label_argv async and fire timed side-effect commands.
 * returns TRUE so g_timeout_add keeps firing.
 */
static gboolean live_tick(gpointer user_data) {
	struct option *opt = (struct option *)user_data;

	/* run timed commands if set (side effects, no label update) */
	if(opt->live->commands)
		spawn_commands(opt->live->commands);

	struct config *owner = (struct config *)opt->live->owner;
	guint          gen   = owner ? owner->reload_gen : 0;

	if(opt->live->update_label_argv) {
		GError      *gerr = NULL;
		GSubprocess *proc = g_subprocess_newv(
		                        (const gchar * const *)opt->live->update_label_argv,
		                        G_SUBPROCESS_FLAGS_STDOUT_PIPE |
		                        G_SUBPROCESS_FLAGS_STDERR_SILENCE,
		                        &gerr);
		if(proc) {
			struct live_cb_ctx *ctx = g_new(struct live_cb_ctx, 1);
			ctx->opt    = opt;
			ctx->proc   = proc;
			ctx->config = owner;
			ctx->gen    = gen;
			g_subprocess_communicate_async(proc, NULL, NULL,
						       on_update_label_done, ctx);
		} else {
			g_error_free(gerr);
		}
	}

	if(opt->live->update_tray_icon_argv) {
		GError      *gerr = NULL;
		GSubprocess *proc = g_subprocess_newv(
		                        (const gchar * const *)opt->live->update_tray_icon_argv,
		                        G_SUBPROCESS_FLAGS_STDOUT_PIPE |
		                        G_SUBPROCESS_FLAGS_STDERR_SILENCE,
		                        &gerr);
		if(proc) {
			struct live_cb_ctx *ctx = g_new(struct live_cb_ctx, 1);
			ctx->opt    = opt;
			ctx->proc   = proc;
			ctx->config = owner;
			ctx->gen    = gen;
			g_subprocess_communicate_async(proc, NULL, NULL,
						       on_update_tray_icon_done, ctx);
		} else {
			g_error_free(gerr);
		}
	}

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

/* ss_lib slot — called when a watch signal is emitted.
 * substitutes {filepath}/{filename} in the command template and spawns it.
 */
/* spawn a shell command from a template string */
static void spawn_tpl_command(const char *cmd_str) {
	if(!cmd_str || '\0' == cmd_str[0])
		return;
	char **argv = g_new(char *, 4);
	argv[0] = g_strdup("sh");
	argv[1] = g_strdup("-c");
	argv[2] = g_strdup(cmd_str);
	argv[3] = NULL;
	g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
		      NULL, NULL, NULL, NULL);
	g_strfreev(argv);
}

static void on_watch_signal(const ss_data_t *data, void *user_data) {
	struct on_watch_block *wb = (struct on_watch_block *)user_data;
	const char *filepath = ss_data_get_string(data);
	if(!filepath)
		return;

	char *filename = g_path_get_basename(filepath);

	if(wb->command_tpl) {
		char *cmd = apply_tpl(wb->command_tpl, filename, filepath, true);
		spawn_tpl_command(cmd);
		g_free(cmd);
	}

	/* fire emit with template substitution on value */
	if(wb->emit && wb->emit->signal) {
		char *val = wb->emit->value
		          ? apply_tpl(wb->emit->value, filename, filepath, false)
		          : NULL;
		struct emit resolved = { .signal = wb->emit->signal, .value = val };
		fire_emit(&resolved);
		g_free(val);
	}

	g_free(filename);
}

/* ss_lib slot — called when a user emit signal is received.
 * substitutes {value} in command_tpl and spawns it. may chain-emit.
 */
static void on_emit_signal(const ss_data_t *data, void *user_data) {
	struct on_emit_block *eb = (struct on_emit_block *)user_data;
	const char *value = ss_data_get_string(data);
	if(!value) value = "";

	if(eb->command_tpl) {
		/* substitute {value} in command template — shell-quote to
		 * prevent injection via crafted signal payloads or filenames
		 */
		char *quoted = shell_quote(value);
		char *cmd = g_strdup(eb->command_tpl);
		if(strstr(cmd, "{value}")) {
			char **parts = g_strsplit(cmd, "{value}", -1);
			g_free(cmd);
			cmd = g_strjoinv(quoted, parts);
			g_strfreev(parts);
		}
		g_free(quoted);
		spawn_tpl_command(cmd);
		g_free(cmd);
	}

	/* chain emit — substitute {value} in the chained value */
	if(eb->emit && eb->emit->signal) {
		char *val = NULL;
		if(eb->emit->value) {
			val = g_strdup(eb->emit->value);
			if(strstr(val, "{value}")) {
				char **parts = g_strsplit(val, "{value}", -1);
				g_free(val);
				val = g_strjoinv(value, parts);
				g_strfreev(parts);
			}
		}
		struct emit resolved = { .signal = eb->emit->signal, .value = val };
		fire_emit(&resolved);
		g_free(val);
	}
}

/* start timers for all live options across all sections of config.
 * independent items get their own g_timeout_add timer.
 * all other live items share one main tick at GCD of their refresh intervals.
 */
static void start_live_timers(struct config *config) {
	GSList *shared_opts = NULL;
	guint   shared_ms   = 0;

	/* apply config-level max_emit_depth override, clamped to 256
	 * to prevent stack overflow from deep recursive emit chains
	 */
	if(0 < config->max_emit_depth) {
		max_emit_depth = config->max_emit_depth;
		if(max_emit_depth > 256) {
			fprintf(stderr, "gensystray: max_emit_depth %d clamped to 256\n",
			        max_emit_depth);
			max_emit_depth = 256;
		}
	}

	/* set ss_lib namespace to instance name for signal scoping */
	ss_set_namespace(config->name ? config->name : "default");

	for(GSList *sl = config->sections; sl; sl = sl->next) {
		struct section *sec = (struct section *)sl->data;
		for(GSList *ol = sec->options; ol; ol = ol->next) {
			struct option *opt = (struct option *)ol->data;
			if(!opt->live)
				continue;

			ss_error_t err = ss_signal_register(opt->live->signal_name);
			if(SS_OK != err && SS_ERR_ALREADY_EXISTS != err)
				fprintf(stderr, "gensystray: failed to register signal '%s': %s\n",
				        opt->live->signal_name, ss_error_string(err));
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

		/* register item-level emit signals */
		for(GSList *ol2 = sec->options; ol2; ol2 = ol2->next) {
			struct option *opt2 = (struct option *)ol2->data;
			if(opt2->emit && opt2->emit->signal)
				ss_signal_register(opt2->emit->signal);
			/* also register emit signals inside on exit/output blocks */
			if(opt2->live) {
				for(GSList *obl = opt2->live->on_blocks; obl; obl = obl->next) {
					struct on_block *ob = (struct on_block *)obl->data;
					if(ob->emit && ob->emit->signal)
						ss_signal_register(ob->emit->signal);
				}
			}
		}

		/* register watch signals for on watch-* blocks */
		for(GSList *wl = sec->on_watch_blocks; wl; wl = wl->next) {
			struct on_watch_block *wb = (struct on_watch_block *)wl->data;
			if(!wb->signal_name)
				continue;
			ss_error_t werr = ss_signal_register(wb->signal_name);
			if(SS_OK != werr && SS_ERR_ALREADY_EXISTS != werr) {
				fprintf(stderr, "gensystray: failed to register watch signal '%s': %s\n",
				        wb->signal_name, ss_error_string(werr));
				continue;
			}
			ss_error_t cerr = ss_connect_ex(wb->signal_name, on_watch_signal, wb,
						        SS_PRIORITY_NORMAL, &wb->conn);
			if(SS_OK != cerr)
				fprintf(stderr, "gensystray: failed to connect watch signal '%s': %s\n",
				        wb->signal_name, ss_error_string(cerr));
			/* also register emit signals from watch on-blocks */
			if(wb->emit && wb->emit->signal)
				ss_signal_register(wb->emit->signal);
		}

		/* register and connect on emit block signals */
		for(GSList *el = sec->on_emit_blocks; el; el = el->next) {
			struct on_emit_block *eb = (struct on_emit_block *)el->data;
			if(!eb->signal_name)
				continue;
			/* register the signal the block listens to */
			ss_signal_register(eb->signal_name);
			ss_error_t eerr = ss_connect_ex(eb->signal_name, on_emit_signal, eb,
						        SS_PRIORITY_NORMAL, &eb->conn);
			if(SS_OK != eerr)
				fprintf(stderr, "gensystray: failed to connect emit signal '%s': %s\n",
				        eb->signal_name, ss_error_string(eerr));
			/* register the chained emit signal if present */
			if(eb->emit && eb->emit->signal)
				ss_signal_register(eb->emit->signal);
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

		/* disconnect and unregister watch signals */
		for(GSList *wl = sec->on_watch_blocks; wl; wl = wl->next) {
			struct on_watch_block *wb = (struct on_watch_block *)wl->data;
			if(0 != wb->conn) {
				ss_disconnect_handle(wb->conn);
				wb->conn = 0;
			}
			if(wb->signal_name)
				ss_signal_unregister(wb->signal_name);
		}

		/* disconnect and unregister emit signals */
		for(GSList *el = sec->on_emit_blocks; el; el = el->next) {
			struct on_emit_block *eb = (struct on_emit_block *)el->data;
			if(0 != eb->conn) {
				ss_disconnect_handle(eb->conn);
				eb->conn = 0;
			}
			if(eb->signal_name)
				ss_signal_unregister(eb->signal_name);
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

/* config file change callback — reload policy lives here in main.
 * re-parses the config, filters to the active instance, stops timers,
 * swaps data, and restarts timers for each running instance.
 */
static void on_cfg_changed(GFileMonitor *monitor, GFile *file,
			   GFile *other_file, GFileMonitorEvent event_type,
			   gpointer user_data)
{
	(void)monitor; (void)file; (void)other_file;
	(void)user_data;

	/* only reload when the file write is complete.
	 * editors that use atomic save (write-to-tmp + rename) emit DELETED
	 * or RENAMED mid-save, and reading the file at that point fails or
	 * returns stale/empty content.  CHANGES_DONE_HINT fires after the
	 * write is finished.  CREATED covers the case where the file was
	 * removed and re-created (or replaced via rename on some monitors).
	 */
	if(event_type != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
	   event_type != G_FILE_MONITOR_EVENT_CREATED)
		return;

	/* close any open menu before swapping data — menu widgets
	 * reference section/option pointers that are about to be freed
	 */
	popdown_all_menus();
	for(GSList *l = all_configs; l; l = l->next)
		disconnect_live_slots((struct config *)l->data);

	GSList *new_list = load_config(app_cfg_path);
	if(!new_list) {
		fprintf(stderr, "gensystray: config reload failed, keeping current config\n");
		for(GSList *l = all_configs; l; l = l->next)
			show_tray_error((struct config *)l->data,
					"config error: reload failed (check stderr)");
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

		/* route old data through matched so free_config_data cleans it up,
		 * and move fresh parsed data into the running config
		 */
		char   *new_icon       = matched->icon_path;
		char   *new_error_icon = matched->error_icon_path;
		char   *new_tooltip    = matched->tooltip;
		GSList *new_sections   = matched->sections;

		matched->icon_path       = old->icon_path;
		matched->error_icon_path = old->error_icon_path;
		matched->tooltip         = old->tooltip;
		matched->sections        = old->sections;

		old->icon_path        = new_icon;
		old->error_icon_path  = new_error_icon;
		old->tooltip          = new_tooltip;
		old->sections         = new_sections;
		old->max_emit_depth   = matched->max_emit_depth;

		/* reset runtime icon to the new parsed default */
		free(old->icon_path_current);
		old->icon_path_current = old->icon_path ? strdup(old->icon_path) : NULL;

		/* also route matched's icon_path_current through free */
		free(matched->icon_path_current);
		matched->icon_path_current = NULL;

		apply_tray_update(old);
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
		if(option->emit) {
			struct click_ctx *ctx = g_new(struct click_ctx, 1);
			ctx->commands = option->commands;
			ctx->emit     = option->emit;
			g_signal_connect_data(G_OBJECT(menu_item), "activate",
					      G_CALLBACK(delegate_system_call_and_emit),
					      ctx, (GClosureNotify)g_free, 0);
		} else if(option->commands) {
			g_signal_connect(G_OBJECT(menu_item), "activate",
					 G_CALLBACK(delegate_system_call),
					 option->commands);
		}
		g_signal_connect_swapped(G_OBJECT(menu_item), "destroy",
					 G_CALLBACK(g_nullify_pointer),
					 &option->live->live_label);
	} else {
		menu_item = gtk_menu_item_new_with_label(option->name);
		if(option->emit) {
			struct click_ctx *ctx = g_new(struct click_ctx, 1);
			ctx->commands = option->commands;
			ctx->emit     = option->emit;
			g_signal_connect_data(G_OBJECT(menu_item), "activate",
					      G_CALLBACK(delegate_system_call_and_emit),
					      ctx, (GClosureNotify)g_free, 0);
		} else {
			g_signal_connect(G_OBJECT(menu_item), "activate",
					 G_CALLBACK(delegate_system_call),
					 option->commands);
		}
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
	struct config *config = (struct config *)user_data;
	const char    *path   = config->icon_path_current
	                      ? config->icon_path_current
	                      : config->icon_path;
	if(!path)
		return FALSE;

	char *expanded = NULL;
	if('~' == path[0]) {
		expanded = g_strconcat(g_get_home_dir(), path + 1, NULL);
		path = expanded;
	}
	GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(path, size, size, NULL);
	g_free(expanded);
	if(pb) {
		gtk_status_icon_set_from_pixbuf(icon, pb);
		g_object_unref(pb);
		return TRUE;
	}
	return FALSE;
}

/* creates a GtkStatusIcon for the given config instance, sets its icon
 * and tooltip, connects the popup-menu signal, and stores the icon pointer
 * in config->tray_icon to keep it alive
 */
static GtkStatusIcon *init_tray(struct config *config) {
	GtkStatusIcon *icon = NULL;

	/* initialize runtime icon path from parsed default */
	if(!config->icon_path_current && config->icon_path)
		config->icon_path_current = strdup(config->icon_path);

	const char *path = config->icon_path_current;

	if(!path) {
		/* no icon specified — fall back to a generic theme icon */
		icon = gtk_status_icon_new_from_icon_name("application-x-executable");
	} else if('/' == path[0] || '.' == path[0] || '~' == path[0]) {
		/* absolute, relative, or ~ path → load as file */
		char *expanded = NULL;
		if('~' == path[0]) {
			expanded = g_strconcat(g_get_home_dir(), path + 1, NULL);
			path = expanded;
		}
		if(!g_file_test(path, G_FILE_TEST_EXISTS)) {
			fprintf(stderr, "gensystray: tray icon file not found: %s\n", path);
			char *msg = g_strdup_printf("config error: tray icon not found: %s", path);
			g_free(expanded);
			/* create a temporary icon, wire up tray_icon so
			 * show_tray_error can operate, then apply the error */
			icon = gtk_status_icon_new_from_icon_name("image-missing");
			config->tray_icon = icon;
			show_tray_error(config, msg);
			g_free(msg);
		} else {
			icon = gtk_status_icon_new_from_file(path);
			g_free(expanded);
		}
	} else {
		/* no path separator → treat as XDG theme icon name */
		icon = gtk_status_icon_new_from_icon_name(path);
	}

	/* always connect size-changed so pixbuf reloads at the exact tray size;
	 * reads icon_path_current at callback time so runtime changes take effect
	 */
	g_signal_connect(G_OBJECT(icon), "size-changed",
			 G_CALLBACK(on_icon_size_changed), config);

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
	platform_init();

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
