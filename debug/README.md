# debug/

Memory profiling and Linux behavior testing. Most contributors will never need this.

## What is here

| File | Purpose |
|------|---------|
| `Dockerfile` | Ubuntu 24.04 image with GCC, GTK, Xvfb, and Valgrind |
| `build.sh` | Build the Docker image — run once before the others |
| `run.sh` | Run gensystray on Linux/GTK without valgrind |
| `run_valgrind.sh` | Run gensystray under Valgrind and report leaks |
| `mesa.supp` | Valgrind suppressions for Mesa/GTK/Pango/fontconfig noise |

## Why Docker + Xvfb

gensystray is a GTK app — it requires a display to initialise. On macOS:

- Valgrind is not natively supported (Apple Silicon + macOS internals break it)
- ASan conflicts with GTK's internal allocators and produces unusable output

The Docker container runs Ubuntu with a headless X11 server (Xvfb) so GTK has
a display to connect to, and Valgrind runs on Linux where it is well supported.

## Prerequisites

- Docker Desktop running

## Typical workflow

```sh
# 1. build the image once (or after code changes)
./debug/build.sh

# 2. check for leaks with the default config
./debug/run_valgrind.sh

# 3. check behaviour on Linux with a specific config
./debug/run.sh /path/to/your.cfg
```

## Reading valgrind output

A clean run ends with:

```
==N== HEAP SUMMARY:
==N==     in use at exit: 0 bytes in 0 blocks
==N==   total heap usage: X allocs, X frees, X bytes allocated
==N==
==N== All heap blocks were freed -- no leaks are possible
==N==
==N== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: Y from Z)
```

Any `definitely lost` or `indirectly lost` block not suppressed is a real leak.
`possibly lost` entries with a gensystray frame in the stack are worth
investigating. Entries with only GTK/GLib/Mesa/Pango frames and no gensystray
frame are framework noise — add them to `mesa.supp`.

## Updating suppressions

If a new GTK or Mesa version introduces new noise, run valgrind manually with
`--gen-suppressions=all`, copy the generated `{ ... }` blocks that have no
gensystray frames into `mesa.supp`, and give them a descriptive name.
