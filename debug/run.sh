#!/usr/bin/env bash
# Run gensystray inside the debug container without valgrind.
# Useful for verifying behaviour on Linux / GTK without macOS differences.
#
# Usage:
#   ./debug/run.sh [config_path]
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
        DISPLAY=:99 ./gensystray
    "
