# GenSysTray 2.2.0

A configurable system tray utility written in C. Click a tray icon to get a menu of commands.

## LICENSE

GPLv3, no later option. See LICENSE or http://www.gnu.org/licenses/gpl-3.0.txt

## Building

Dependencies: `gtk+3`, `gcc`, `make`, `pkg-config`

```sh
make init   # fetch submodules (libucl, ss_lib)
make        # build
make run    # build and run
```

## Configuration

Config file location: `~/.config/gensystray/gensystray.cfg`

Override with: `GENSYSTRAY_PATH=/path/to/file gensystray`

### Single instance

```hcl
tray {
  icon    = "/path/to/icon.png"
  tooltip = "My Tray"
}

item "Terminal" {
  command = "xterm"
}

item "Browser" {
  command = "firefox"
}

item "separator" {
  separator = true
}

item "Editor" {
  command = "nvim"
}
```

### Multiple instances

One config file can declare multiple tray icons via `instance` blocks.
No top-level items or tray block are allowed when using instances.
Start a specific instance with `--instance <name>`.

```hcl
instance "work" {
  tray {
    icon    = "/path/to/work.png"
    tooltip = "Work"
  }

  item "Terminal" {
    command = "xterm"
  }
}

instance "personal" {
  tray {
    icon    = "/path/to/personal.png"
    tooltip = "Personal"
  }

  item "Music" {
    command = "spotify"
  }
}
```

### Commands

Four forms are supported:

```hcl
# string — runs via $SHELL -c, falls back to sh
item "Date" {
  command = "date >> /tmp/log.txt"
}

# array — direct exec, no shell, arguments are safe from splitting
item "Editor" {
  command = ["nvim", "--listen", "/tmp/nvim.sock"]
}

# multiple commands — array of arrays, all run on click
item "Deploy" {
  command = [
    ["make", "build"],
    ["make", "deploy"]
  ]
}

# heredoc with shebang — explicit interpreter per item
item "Script" {
  command = <<EOD
#!/usr/bin/env python3
import datetime
with open("/tmp/log.txt", "a") as f:
    f.write(str(datetime.datetime.now()) + "\n")
EOD
}
```

### Item ordering

Items with `order` come first, sorted by value. Items without `order` follow
in declaration order. Separators obey the same rules.

```hcl
item "First" {
  command = "echo first"
  order   = 1
}

item "Second" {
  command = "echo second"
  order   = 2
}

item "Appended" {
  command = "echo appended"
}
```

### Live items

Live items run on a timer via a `live` block. `refresh` is required.
Supported units: `ms`, `s`, `m`, `h`.

`update_label` runs a command on each tick and displays the stdout as the
item label. `command` inside `live` runs on the timer as a side effect
with no label change. Both are optional and independent.

The click action (`command` at item level) is separate from timer behavior.

```hcl
# label updates every second, clicking opens Calendar
item "Clock" {
  command = ["open", "-a", "Calendar"]

  live {
    refresh      = "1s"
    update_label = "date '+%H:%M:%S'"
  }
}

# label updates every 5 seconds, no click action
item "Uptime" {
  live {
    refresh      = "5s"
    update_label = "uptime | awk '{print $3}'"
  }
}

# runs a sync every 5 minutes, label stays static
item "Sync" {
  live {
    refresh = "5m"
    command = ["rsync", "-av", "~/docs/", "backup:/docs/"]
  }
}
```

By default all live items share a single master timer running at the GCD
of their refresh intervals. Add `independent = true` to give an item its
own timer:

```hcl
item "Sensor" {
  live {
    refresh      = "100ms"
    update_label = "cat /dev/sensor"
    independent  = true
  }
}
```

### Reactive on blocks

Inside a `live` block, `on exit` and `on output` react to the
`update_label` command result. First match wins. Commands inside an `on`
block fire only on state transition (not every tick).

```hcl
item "Service" {
  live {
    refresh      = "5s"
    update_label = ["sh", "-c", "curl -s -o /dev/null -w '%{http_code}' http://localhost:8080/health"]

    on exit 0 {
      label   = "UP"
      command = ["notify-send", "Service recovered"]
    }

    on exit 1 { label = "DOWN" }

    on output "200" { label = "Healthy" }
    on output "503" { label = "Degraded" }
  }
}
```

- `on exit N` matches the exit code of `update_label`
- `on output "str"` matches when stdout contains `str` (substring match)
- `label` overrides the displayed text for that state
- `command` (optional) runs once when entering that state, supports all
  command forms including multiple commands

### Live tray icon

`update_tray_icon` runs a command on each tick. Its stdout becomes the new
tray icon path, overriding the parsed default until the next config reload.
An empty or failed stdout leaves the icon unchanged. The icon resets to the
config default on reload.

```hcl
# cycle through icons based on a runtime condition
item "Status" {
  live {
    refresh          = "5s"
    update_label     = "my-status-script --label"
    update_tray_icon = "my-status-script --icon"
  }
}
```

The icon path rules are the same as the `tray { icon = ... }` field:
`./file.png`, `/abs/path.png`, `~/path.png` load as files; bare names like
`"battery-low"` are XDG theme icon names.

### Sections

Sections group items into submenus (collapsed) or inline groups (expanded).

```hcl
# collapsed — renders as a submenu
section "Tools" {
  item "Htop" {
    command = ["xterm", "-e", "htop"]
  }
  item "Logs" {
    command = ["xterm", "-e", "tail -f /var/log/syslog"]
  }
}

# expanded — renders inline with separators
section "Status" {
  expanded   = true
  separators = true   # "top", "bottom", "none", or true (both)
  show_label = true

  item "Clock" {
    live {
      refresh      = "1s"
      update_label = "date '+%H:%M:%S'"
    }
  }
}
```

### Dynamic sections (glob populate)

Sections can be populated dynamically from the filesystem:

```hcl
section "Notes" {
  expanded = true

  populate {
    from    = "glob"
    pattern = "~/notes/*.md"
    watch   = true        # re-expands when files are added or removed
    depth   = 0           # 0 = current dir only, N = N levels, -1 = unlimited
    hierarchy = false     # true = subdirs become submenus

    item {
      label   = "{filename}"
      command = ["nvim", "{filepath}"]
    }
  }
}
```

Multiple populate blocks per section are supported — each with its own
pattern and item template:

```hcl
section "Documents" {
  expanded = true

  populate {
    from    = "glob"
    pattern = "~/docs/*.md"
    watch   = true
    item {
      label   = "{filename}"
      command = ["nvim", "{filepath}"]
    }
  }

  populate {
    from    = "glob"
    pattern = "~/docs/*.pdf"
    watch   = true
    item {
      label   = "{filename}"
      command = ["zathura", "{filepath}"]
    }
  }
}
```

## FAQ

**Why no later option on the license?**
I can't agree with a license that doesn't exist yet. Conceding such rights
would be irresponsible.

**Why a Generic System Tray Icon?**
Sometimes you run scripts so often that a single click is better than
opening a terminal every time.
