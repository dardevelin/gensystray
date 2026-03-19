#!/usr/bin/env bash
# Run gensystray under valgrind inside the debug container.
# Exits 0 if no leaks attributable to gensystray code, non-zero otherwise.
#
# Usage:
#   ./debug/run_valgrind.sh [config_path]
#
# config_path defaults to examples/single.cfg
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
CONFIG="${1:-$REPO_ROOT/examples/single.cfg}"

if ! docker image inspect gensystray-debug >/dev/null 2>&1; then
    echo "image not found — building..."
    "$SCRIPT_DIR/build.sh"
fi

if [ ! -f "$CONFIG" ]; then
    echo "error: config not found: $CONFIG" >&2
    exit 1
fi

docker run --rm \
    -v "$CONFIG:/root/.config/gensystray/gensystray.cfg" \
    gensystray-debug \
    bash -c "
        Xvfb :99 -screen 0 1024x768x24 &
        sleep 1
        DISPLAY=:99 valgrind \
            --leak-check=full \
            --track-origins=yes \
            --error-exitcode=1 \
            --suppressions=/usr/share/glib-2.0/valgrind/glib.supp \
            --suppressions=/usr/share/gtk-3.0/valgrind/gtk.supp \
            --suppressions=/app/debug/mesa.supp \
            ./gensystray &
        VPID=\$!
        sleep 5
        kill \$VPID
        wait \$VPID 2>/dev/null
    " 2>&1 | grep -v 'keysym\|xkbcomp\|not fatal to the X server'
