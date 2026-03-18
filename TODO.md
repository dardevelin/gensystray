# gensystray 2.0 TODO

## Config format (locked)

The 2.0 config format is HCL. The following structure is locked:

```hcl
tray {
  icon    = "/path/to/icon.png"
  tooltip = "My Tray"
}

item "Terminal" {
  command = ["xterm"]
}

item "CPU" {
  live    = "..."
  refresh = "2s"
}

section "Notes" {
  populate {
    from    = "glob"
    pattern = "~/notes/*.md"
    watch   = true
    item {
      label   = "{filename}"
      command = ["nvim", "{filepath}"]
    }
  }

  populate {
    from    = "glob"
    pattern = "~/notes/*.pdf"
    watch   = true
    item {
      label   = "{filename}"
      command = ["zathura", "{filepath}"]
    }
  }
}

ipc {
  socket = "/run/user/1000/gensystray.sock"
}
```

## Needs design

- live items: define what `live` accepts — single command string, array, or
  heredoc. Define how stdout is consumed (full output as label, last line,
  trimmed). Define error behavior when the command fails or returns empty.

- populate sources: `glob` is locked. Design `live` (stdout lines as entries)
  and `ipc` (external process pushes entries over socket) sources.

- ipc protocol: define the message format over the unix socket. Operations
  needed: reload config, push items to a section, remove items from a section,
  query current state.

## Pending C cleanup

- UTF-8 audit: fstrchr and fextract are byte-level and safe for ASCII
  delimiters but this assumption is undocumented.

## Done

- Replace build scripts with Makefile using pkg-config
- Replace SDL2 thread spawner with g_spawn_async
- Replace dlist with GSList
- Isolate config parser (static internals, public load_config/free_config)
- Add config file monitor (GFileMonitor, live reload)
- Unify error codes, drop gensystray_errors.h
