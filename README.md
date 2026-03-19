<div align="center">
  <img src="assets/icons/icon_128.png" width="96" alt="GenSysTray" />
  <h1>GenSysTray</h1>
  <p>Your scripts, one click away. Configurable system tray utility for macOS and Linux.</p>

  <a href="https://github.com/dardevelin/gensystray/releases/latest"><img src="https://img.shields.io/github/v/release/dardevelin/gensystray?color=%231D9E75&label=latest" alt="Latest release"/></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPLv3-blue.svg" alt="License"/></a>
  <a href="https://github.com/dardevelin/gensystray/releases/latest"><img src="https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey" alt="Platform"/></a>
</div>

---

GenSysTray sits quietly in the menu bar or system tray. Click the icon to get a menu of commands, scripts, and live status items — everything defined in a plain text config file. No GUI. No daemon. Just your tools, close at hand.

The config hot-reloads on save. Live items update on a timer. The tray icon itself can change based on what your scripts report. It stays out of the way until you need it.

## Install

### macOS — Homebrew

```sh
brew tap dardevelin/tap
brew install --cask gensystray
```

Or standalone:

```sh
brew tap dardevelin/gensystray
brew install --cask gensystray
```

### macOS — Direct download

Download the latest `GenSysTray-<version>-macos.zip` from [Releases](https://github.com/dardevelin/gensystray/releases), unzip, and move `GenSysTray.app` to `/Applications`.

The app is signed and notarized — Gatekeeper will not block it.

### Linux — Build from source

```sh
git clone https://github.com/dardevelin/gensystray
cd gensystray
make init
make
./gensystray
```

Dependencies: `gtk+3`, `gcc`, `make`, `pkg-config`

## Configuration

Config file: `~/.config/gensystray/gensystray.cfg`

Override: `GENSYSTRAY_PATH=/path/to/config.cfg`

### Minimal example

```hcl
tray {
  icon    = "/path/to/icon.png"
  tooltip = "My Tray"
}

item "Terminal" { command = "xterm" }
item "Browser"  { command = "firefox" }

item "separator" { separator = true }

item "Editor" { command = "nvim" }
```

### Multiple instances

One config file, multiple tray icons. Start each with `--instance <name>`.

```hcl
instance "work" {
  tray { icon = "/path/to/work.png" tooltip = "Work" }
  item "Terminal" { command = "xterm" }
}

instance "personal" {
  tray { icon = "/path/to/personal.png" tooltip = "Personal" }
  item "Music" { command = "spotify" }
}
```

### Commands

```hcl
# string — runs via $SHELL -c
item "Date" { command = "date >> /tmp/log.txt" }

# array — direct exec, no shell
item "Editor" { command = ["nvim", "--listen", "/tmp/nvim.sock"] }

# multiple commands — all run on click
item "Deploy" {
  command = [
    ["make", "build"],
    ["make", "deploy"]
  ]
}

# heredoc with shebang — explicit interpreter
item "Script" {
  command = <<EOD
#!/usr/bin/env python3
import datetime
with open("/tmp/log.txt", "a") as f:
    f.write(str(datetime.datetime.now()) + "\n")
EOD
}
```

### Live items

Items that update on a timer. `refresh` is required (`ms`, `s`, `m`, `h`).

```hcl
item "Clock" {
  command = ["open", "-a", "Calendar"]
  live {
    refresh      = "1s"
    update_label = "date '+%H:%M:%S'"
  }
}

item "Uptime" {
  live {
    refresh      = "5s"
    update_label = "uptime | awk '{print $3}'"
  }
}
```

All live items share a single timer at the GCD of their refresh intervals. Use `independent = true` for a dedicated timer:

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

React to exit codes or output — commands fire only on state transition.

```hcl
item "Service" {
  live {
    refresh      = "5s"
    update_label = ["sh", "-c", "curl -s -o /dev/null -w '%{http_code}' http://localhost:8080/health"]

    on exit 0        { label = "UP"       command = ["notify-send", "Service recovered"] }
    on exit 1        { label = "DOWN" }
    on output "200"  { label = "Healthy" }
    on output "503"  { label = "Degraded" }
  }
}
```

### Live tray icon

The tray icon itself can change based on script output. Resets to config default on reload.

```hcl
item "Status" {
  live {
    refresh          = "5s"
    update_label     = "my-status-script --label"
    update_tray_icon = "my-status-script --icon"
  }
}
```

Icon path rules: `./file.png`, `/abs/path.png`, `~/path.png` load as files; bare names like `"battery-low"` are XDG theme icon names.

### Sections

```hcl
# collapsed — renders as a submenu
section "Tools" {
  item "Htop" { command = ["xterm", "-e", "htop"] }
  item "Logs" { command = ["xterm", "-e", "tail -f /var/log/syslog"] }
}

# expanded — renders inline
section "Status" {
  expanded   = true
  separators = true
  show_label = true

  item "Clock" {
    live { refresh = "1s" update_label = "date '+%H:%M:%S'" }
  }
}
```

### Dynamic sections (glob populate)

Populate a section from the filesystem, with live watching.

```hcl
section "Notes" {
  expanded = true

  populate {
    from      = "glob"
    pattern   = "~/notes/*.md"
    watch     = true
    hierarchy = false

    item {
      label   = "{filename}"
      command = ["nvim", "{filepath}"]
    }
  }
}
```

### Item ordering

Items with `order` come first, sorted by value. Items without follow in declaration order.

```hcl
item "First"    { command = "echo first"    order = 1 }
item "Second"   { command = "echo second"   order = 2 }
item "Appended" { command = "echo appended" }
```

## Building from source (macOS)

```sh
make app      # builds GenSysTray.app (release, no dock icon)
make sign SIGN_ID="Developer ID Application: Name (TEAMID)"
make notarize APPLE_ID=you@example.com APPLE_TEAM=TEAMID APPLE_PASS=xxxx-xxxx-xxxx-xxxx
```

## License

GPLv3, no later option. See [LICENSE](LICENSE) or https://www.gnu.org/licenses/gpl-3.0.txt

**Why no later option?** I can't agree to a license that doesn't exist yet.

## FAQ

**Why a Generic System Tray Icon?**
Sometimes you run scripts so often that a single click is better than opening a terminal every time.
