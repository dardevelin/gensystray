# Security Audit — 2026-03-19

## Summary

6 vulnerabilities confirmed via proof-of-concept exploits in Docker/Valgrind.
All findings are in user-space — the config file runs with the same privilege
as the user. However, Finding 1+2 are exploitable via crafted filenames on
disk (no config access needed).

All findings have been fixed and verified.

## Confirmed Findings

### Finding 1: Command Injection via {value} in on_emit_signal [Critical]

**File:** gensystray.c, `on_emit_signal`

**Root cause:** `{value}` substituted into command template without shell
quoting, then passed to `sh -c`. Unlike `on_watch_signal` which uses
`apply_tpl()` with `for_shell=true`, `on_emit_signal` did raw `g_strjoinv`
with the unescaped payload.

**PoC:** Live item with `on exit 0 { emit { signal = "inject"; value = "x; touch /tmp/pwned; echo y" } }`
combined with `on emit "inject" { command = "echo {value}" }`.
Result: `/tmp/pwned` created via injected shell command.

**Fix:** Apply `shell_quote()` to `{value}` before substitution in command
templates, matching the pattern used in `on_watch_signal`.

### Finding 2: Command Injection Chain via Watch → Emit [High]

**File:** gensystray.c, `on_watch_signal`

**Root cause:** Watch emit values used `apply_tpl()` with `for_shell=false`,
so `{filepath}/{filename}` were NOT shell-quoted in the emit value. When this
flowed to an `on emit` handler (Finding 1), the unquoted path reached `sh -c`.

**Fix:** Fixed by Finding 1's fix — `{value}` is now always shell-quoted at
consumption time in `on_emit_signal`.

### Finding 3: Integer Overflow / DoS in parse_duration_ms [Medium]

**File:** gensystray_config_parser.c, `parse_duration_ms`

**Root cause:** `val * 1000` computed in 64-bit long then truncated to 32-bit
guint without overflow check. No minimum value enforced.

**PoC A:** `refresh = "1ms"` spawned 1,829 processes in 3 seconds (~609/sec).

**PoC B:** `refresh = "4294968s"` (intended ~50 days) wrapped to 704ms after
guint truncation, causing unintended high-frequency polling.

**Fix:** Enforce minimum 100ms, clamp overflow to 24h, check bounds before cast.

### Finding 4: Stack Overflow via Symlink Cycle in Glob Recursion [Medium]

**File:** gensystray_config_parser.c, `expand_dir`, `watch_dir`

**Root cause:** `depth = -1` caused unlimited recursion. No symlink detection.
`g_file_test(IS_DIR)` follows symlinks, so a symlink cycle caused infinite
recursion and stack overflow.

**PoC:** `mkdir -p /tmp/a/b && ln -s /tmp/a /tmp/a/b/cycle` with
`pattern = "/tmp/a/*"; depth = -1`. Exit 139 (SIGSEGV). Even `/usr/share/*`
with `depth = -1` overflowed the stack without symlinks.

**Fix:** Cap `depth = -1` to 64 internally. Skip symlinked directories via
`G_FILE_TEST_IS_SYMLINK` check in both `expand_dir` and `watch_dir`.

### Finding 5: NULL Dereference on Wrong Config Value Types [Medium]

**File:** gensystray_config_parser.c, multiple locations

**Root cause:** `ucl_object_tostring()` returns NULL for non-string types.
Return value passed to `strdup(NULL)` caused SIGSEGV.

**Confirmed crashes:**
- `tooltip = true` (boolean instead of string)
- `from = 123` in populate (integer instead of string)

**Fix:** Added `ucl_tostring_safe()` wrapper that returns a fallback string
instead of NULL. Applied to all `ucl_object_tostring` → `strdup` paths.

### Finding 6: Stack Overflow via max_emit_depth Override [Medium]

**File:** gensystray.c, `fire_emit`, `start_live_timers`

**Root cause:** `tray { max_emit_depth = 100000 }` accepted without upper bound.
Circular emit chain recursed ~10,000-30,000 frames before 8MB stack exhaustion —
well before the depth guard fired.

**PoC:** Circular emit with `max_emit_depth = 100000`. Exit 139 (SIGSEGV).
Default depth=16 survived cleanly.

**Fix:** Clamp `max_emit_depth` to hard ceiling of 256 with warning.

## Not Exploitable (Confirmed Safe)

- **Use-after-free in reload path:** Safe due to GTK single-threaded main loop.
- **Path traversal:** Config runs as current user — same filesystem access by design.
- **Raw text parser bounds:** Null terminator prevents out-of-bounds reads.
- **Signal name collision:** Config correctness issue, not a security vulnerability.
- **Buffer truncation in error messages:** `snprintf` prevents overflow.
