# gensystray TODO

## Done

### Core
- Replace build scripts with Makefile using pkg-config
- Replace SDL2 thread spawner with g_spawn_async
- Replace dlist with GSList
- Isolate config parser (static internals, public load_config/free_config)
- Add config file monitor (GFileMonitor, live reload)
- Unify error codes, drop gensystray_errors.h
- Single-instance and multi-instance (`instance "name" {}`) config modes
- Item ordering: explicit `order` field, then declaration order
- Separators as items with `separator = true`

### Config format
- UCL-based config (superset of HCL) via libucl
- `tray { icon = ... tooltip = ... }` block
- `item "Name" { command = ... }` — string, array, or array-of-arrays
- `section "Name" { expanded = ... }` with `show_label`, `separators`, `order`
- Shell detection: string commands use `$SHELL`, shebang (`#!`) overrides per item
- Proper error messages for malformed config (no crashes on bad syntax)
- UTF-8 safe custom parser (byte-level ASCII scanning, match strings as raw bytes)

### Live items
- `live { refresh = "1s" update_label = ... }` block
- Async `update_label` via GSubprocess (non-blocking, no GTK stall)
- `independent = true` — per-item timer instead of shared master tick
- Master tick at GCD of all non-independent refresh intervals
- `on exit N { label = ... command = ... }` blocks
- `on output "str" { label = ... command = ... }` blocks
- State transition — on-block commands fire only when state changes
- Custom mini-parser for `on` blocks (UCL merges duplicate keys, can't handle these natively)
- UCL sanitisation: `on exit`/`on output` lines blanked before UCL sees them

### Glob populate
- `populate { from = "glob" pattern = "~/..." watch = true }` sections
- `{filename}` and `{filepath}` template substitution in label and command
- Shell-safe single-quoting for `{filepath}` in sh -c commands (spaces in filenames)
- `depth = N` — recursion depth (0 = base dir only, N = N levels, -1 = unlimited)
- `hierarchy = true` — subdirectories become nested submenu items
- `hierarchy_expanded = true` on section — subdir contents render flat with dim header
- `watch = true` — GFileMonitor re-expands on filesystem change

### Icons
- File path icons (`/abs/path`, `~/path`, `./rel`) loaded via GdkPixbuf
- `size-changed` signal reloads pixbuf at exact tray-requested size (22, 24, 32px)
- XDG theme icon names (`"battery"`, `"network-wireless"`) via gtk_status_icon_new_from_icon_name
- Fallback to `application-x-executable` theme icon when no icon specified
- `icon_path_current` — runtime-mutable icon path, initialized from config, overridable by live commands
- `update_tray_icon` in live block — stdout of command sets `icon_path_current` each tick
- Icon resets to config default on reload; fallback chain: current → parsed default → system icon

### Build
- Cross-platform: macOS (Clang + ObjC NSEvent monitor) and Linux (GCC + GTK stub)
- `make` auto-runs `make init` if submodules not initialised
- `make release` — LTO (`-flto=thin` macOS / `-flto` Linux), `-Os`, dead-strip, strip
- macOS-specific menu-dismiss code behind `#ifdef __APPLE__`
- libucl and ss_lib built from submodules, no system install needed
- `make app` (macOS) — builds `GenSysTray.app` bundle with `LSUIElement` (no dock icon), embeds default icon
- `make sign SIGN_ID="..."` — codesigns the bundle with a Developer ID
- `make notarize APPLE_ID=... APPLE_TEAM=... APPLE_PASS=...` — submits to Apple notary, staples ticket

### Examples
- `single.cfg` — minimal single-instance config
- `multi.cfg` — multiple instances
- `sections.cfg` — named sections, separators, ordering
- `commands.cfg` — string, array, and multi-command items
- `glob_populate.cfg` — populate from filesystem glob
- `live_on_blocks.cfg` — clock variants, independent timer, on exit/output blocks
- `tray_icon_reload.cfg` — hot-reload tray icon by editing config and saving
- `live_tray_icon.cfg` — live command drives tray icon path each tick

---

## Deferred

### IPC
- Unix socket protocol (`ipc { socket = "..." }` block)
- `gensystray-msg` CLI tool to send commands to a running instance
- Operations: reload config, push/remove items in a section, query state
- Multi-instance socket naming

### Process lifetime
- Process stays alive when all instances are closed (for file monitor resurrection)
- Currently exits when last instance menu is dismissed

### macOS tray icon tinting (future)
- Auto-tint user icons to match macOS menu bar appearance (light/dark mode)
- Detect system appearance via NSAppearance in ObjC layer at startup and on change
- Desaturate + recolor the icon pixbuf to match system theme automatically
- Watch for appearance change notifications and call apply_tray_update
- Goal: any user-supplied icon works as a native-feeling menu bar icon without manual variants

### Live items (future)
- `toggle` syntax in live blocks — flip between two states (label, icon, command) each tick or on condition
- `populate { from = "live" }` — stdout lines as dynamic section entries
- `populate { from = "ipc" }` — external process pushes entries over socket

### Memory
- Arena allocator for config structs (reduce malloc fragmentation on reload)
- Currently uses malloc/free per struct on every config reload

### Live items in hierarchy submenus
- `start_live_timers` does not recurse into `opt->subopts`
- Live items nested inside hierarchy submenus never get timers started
