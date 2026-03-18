# GenSysTray

A configurable system tray utility written in C. Click a tray icon to get a menu of commands.

## LICENSE

GPLv3, no later option. See LICENSE or http://www.gnu.org/licenses/gpl-3.0.txt

## Building

Dependencies: `gtk+3`, `gcc`, `make`, `pkg-config`

```sh
make init   # fetch submodules (libucl)
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

One config file can declare multiple tray icons. Everything must be inside
`instance` blocks — no top-level items or tray block allowed.

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

### Item ordering

Items without `order` appear in declaration order. Items with `order` come
first, sorted by value. Separators follow the same rules.

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

## FAQ

**Why no later option on the license?**
I can't agree with a license that doesn't exist yet. Conceding such rights
would be irresponsible.

**Why a Generic System Tray Icon?**
Sometimes you run scripts so often that a single click is better than
opening a terminal every time.
