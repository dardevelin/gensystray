# Gensystray - Version 2

A configurable System Tray Icon.
Have your common routines right on your system tray.



## License

Gensystray is licensed under the GPLv3 No Later Option.

Dismiss any references to later versions in the LICENSE file.

To know more about your rights, go to [GPLv3](http://www.gnu.org/licenses/gpl-3.0.txt)

## Running Gensystray - Version 2

**Moving from Gensystray Version 1 to Version 2**

```
gensystray-migrate path/to/old/gensystray.cfg path/to/new/gensystray_config.toml
```


TODO

## Installation

To install Gensystray, follow these steps:

TODO

## Usage | How to Configure Gensystray

### Introduction 

`gensystray` is configured using a `config.toml` file that defines the behavior of system tray icons and their associated commands. 

## Configuration File Structure

### 1. General Settings

At the top level, you can define settings that affect all instances of `gensystray`

```toml
gensystray_version = "2.0.0"

[gensystray]
hot_reload = true # Automatically reload configuration when the file changes.
instances_number = 3 # Number of system tray icons to create
trigger_mode = "click" # Triger mode for icons: "click" or "hover"
keep_history = false # Wheather to keep a history of executed commands
history_file = "~/.gensystray/logs/history.log" #Path to the history file
history_limit = 100 # Maximum number of history entries to keep (0 is unlimited)
```

### 2. Instance Configuration

Each system tray icon is considered an "instance" and has its own configuration block. <br/>
You can define multiple instances, each with its own commands and behavior.

**Instance 1: Example Configuration**
```toml
[instance_1.list]
tooltip = "Game List" # Custom tooltip for the tray icon
icon = "~/.gensystray/assets/console_handle_icon.png"
ordering_strategy = "linear" # Command ordering: "linear", "numeral", or "alphabetical"

[[instance_1.list.commands]]
name = "Counter-Strike: Global Offensive"
numeral_order = 1
notify = "Counter-Strike: Global Offensive is now running!"
command = """
steam steam://rungameid/730
"""

[[instance_1.list.commands]]
name = "Guild Wars 2"
numeral_order = 2
notify = "Guild Wars 2 is now running"
command = """
steam steam://rungameid/1289380
"""

[[instance_1.list.commands]]
name = "The Sims 4"
numeral_order = 3
notify = ""
command = """
steam steam://rungameid/730
"""
```

### 3. Advanced Features: Live Commands
You can configure commands to run continuously in the background and update the tray icon based on their status.

**Instance 3: Live Command Example**
```toml
[instance_3.list]
tooltip = "Live Streams List"
icon = "~/.gensystray/assets/twitch_logo.png"
ordering_strategy = "numeral"

[[instance_3.list.commands]]
name = "Twitch Stream is Live"
notify = ""
command = """
~/.gensystray/my_scripts/check_favorite_twitch_streamer_is_live.sh
"""
is_live_command = true # This command runs in the background
live_refresh = 5 # Refresh Rate in seconds

# Define actions based on event codes returnyised by the live command
live_refresh_event_codes = [0, 1, 2, 3]
live_refresh_event_code_0_command = "" #command to run for event code 0
live_refresh_event_code_1_command = "" #command to run for event code 1
live_refresh_event_code_2_command = "" #command to run for event code 2
live_refresh_event_code_3_command = "" #command to run for event code 3
```

### 4. Ordering Strategies

`gensystray` allows you to order the commands in different ways.

- `linear`: Commands appear in the order they are defined.
- `numeral`: Commands are ordered based on `numeral_order`field.
- `alphabetical`: Commands are ordered alphabetically by their `name`.

### 5. Multi-Line Commands

You can write complex, multi-line commands using triple quotes (`"""`):

```toml
[[instance_2.list.commands]]
name = "Coding Focus Mode"
notify = "Coding Mode Activated"
command = """
music_player ~/Music/coding/*mp3 &&
visual-studio-code ~/codebases
"""
```

### Conclusion

The `gensystray_config.toml` file for `gensystray`is highly customizable, allowing you to define multiple <br/>
instances, each with its own set of commands, icons and behaviors. By editing this file, you can <br/>
tailor `gensystray`for fit your specific workflow and requirements.

## Contributing

We welcome contributions from the community. To contribute to Gensystray, please follow these guidelines:

1. Step 1: Fork the repository.
2. Step 2: Create a new branch.
3. Step 3: Make your changes and commit them.
4. Step 4: Push your changes to your forked repository.
5. Step 5: Submit a pull request.

## Migration from Version 1 to Version 2

To migrate from Gensystray Version 1 to Version 2, follow these steps:

TODO

## Continuous Integration (CI) Pipeline Checks

To ensure the quality and stability of your codebase, it is recommended to set up a CI pipeline with the following checks:

TODO

## Logo

![Gensystray Logo](assets/logo_with_grey_background_1024x1024.png)