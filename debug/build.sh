#!/usr/bin/env bash
# Build the gensystray debug Docker image.
# Run this once before using run.sh or run_valgrind.sh.
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
docker build -f "$SCRIPT_DIR/Dockerfile" -t gensystray-debug "$REPO_ROOT"
echo "image ready: gensystray-debug"
