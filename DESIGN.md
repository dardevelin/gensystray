# Design Tenets<br/>
Use Composition<br/>
Use Functional Design Patterns<br/>
Model using Data-Oriented-Design Principles<br/>
Use Logical Composition<br/>
Prefer Immutable Objects.<br/>
Use UTF-8 everywhere by default.<br/>
Use Errors as Values.<br/>
Use Translatable Strings.<br/>
Use Composition for Platform Specific Code<br/>
Prefer Simple Types<br/>

## Application Modules - for Composition

- [ ] **Configuration Module**
- [ ] **Command Executor Module**
- [ ] **Live Command Manager Module**
- [ ] **Localization and UTF8 Handling Module**
- [ ] **Error Handling and Logging Module**
- [ ] **Application Entry Point and Orchestration Module**
- [ ] **Testing and Utility Module**
- [ ] **Embedded Assets Module**
- [ ] **Tray Icon Manager Module**
- [ ] **Menu Builder Module**
- [ ] **Plarform Abstraction Module**
- [ ] **Application Modules - for Composition**
- [ ] **Configuration Module**
- [ ] **Command Executor Module**
- [ ] **Live Command Manager Module**
- [ ] **Localization and UTF8 Handling Module**
- [ ] **Error Handling and Logging Module**
- [ ] **Application Entry Point and Orchestration Module**
- [ ] **Testing and Utility Module**
- [ ] **Embedded Assets Module**
- [ ] **Tray Icon Manager Module**
- [ ] **Menu Builder Module**
- [ ] **Plarform Abstraction Module**


### Configuration Module

Handles loading, parsing and validation of the configuration file. `gensystray_config.toml`<br/>
Ensures that the application can dynamically read its settings and adjust its behavior.

- **Configuration Parser**: Responsible for reading the TOML file and converting into Rust Data<br/>
structures. This should handle errors gracefully and ensure all required fields are present.
- **Validation Logic**: Ensures that all configurations are valid, such as checking if `ordering_strategy`<br/> is one of the allowed values or if paths to icons and scripts are valid.
- **Hot Reload Mechanism**: (Optional) Watches the configuration file for changes and triggers a reload if `hot_reload`is enabled.
- **Undo Reload Configuration**: (Optional) During a time window, it allows the user to unload the latest configuration.

### Command Executor Module

Executes the commands associated with each tray icon and manages their output, including<br/>
handling notifications and history logging.

- **Command Runner**: Executes shell commands or scripts when a tray icon's menu item is selected.<br/>This component should handle both synchronous and asynchronous execution, depending on commands nature.
- **Notification System**: Sends notifications to the user when a command is executed, using the `notify` field from configuration. This too will be platform speciifc.
- **History Logger**: if `keep_history`is enabled, this component logs executed commands along with timestamps to the specified history file.


### Live Command Manager Module
Manages live commands that run continuously in the background, updating the tray icon or menu based on the commands output.

- **Live Command Runner**: Executes live commands at the specified refresh interval (`live_refresh`) and handles their output.
- **Event Code Handler**: Processes event codes returned by live commands and triggers corresponding actions, as defined in the configuration.
- **State Updater**: Updates the tray icon or menu based on the current state of the live command, ensuring that the user is presented with the most current information.

### Localization and UTF8 Handling Module

Manages all user-facing strings and ensures that they are translatable and encoded in UTF-8.
This Module ensures that the application can be localized into different languages.

- **String Manager**: Loads and manages translatable strings, ensuring they are correctly localized based on the user's preference or system settings.
- **Encoding Validator**: Ensures that all strings are correctly encoded in UTF-8, especially when interacting with external systems or files.

### Error Handling and Logging Module

Centralizes error handling, ensuring that errors are treated as values and can be composed and passed around. This module also handles logging for debugging and operational monitoring.

- **Error Type Definitions**: Defines error types for different parts of the application, allowing for precise error handling and recovery.
- **Error Propagation**: Ensures that errors can be propagated and handled at the appropriate level of the application, without causing panics.
- **Logger**: Provides logging capabilities to track errors, warnings, and other significant events during the application's runtime.

### Application Entry Point and Orchestration Module

Serves as a main entry point for the application, orchestrating the initialization and coordination of all other modules

- **Startup Manager**: Initalizes the configuration, sets up the tray icons and starts the command executors and live command managers.
- **Main Event Loop**: Manages the main event loop of the application, ensuring that the application remains responsive and performs its tasks efficiently.

### Testing and Utility Module

Utilities for testing, mock implementations, and other helper functions that are used throughout the application when tests are conducted.

- **Test Utilities**: Functions and types that simplify writing tests for different parts of the application, ensuring that components behave correctly in isonlation and when composed.
- **Mock Implementations**: Mock versions of platform-specific integrations or command runners to faciliate unit testing without requiring the actual platform or external dependencies.

### Embedded Assets Module

Embedded Assets Module provides a virtual filesytem where assets can be loaded and be made available<br/>
for other modules to consume.

- References Assets On Startup
- Uses Lazy Loading by Default for assets. Only when the UI tries to read, will it lead from disk.
- Support for Eager Loading support.
- Support for Virtual Filesytem, 

### Tray Icon Manager Module

Manages the creation, updating, and removal of system tray icons based on the configuration.<br/>
Each tray icon is associated with an "instance" in the configuration.

- **Icon Renderer**: Handles the loading and rendering of icons in the system tray.<br/>This component should handle both synchronous and asynchronous execution baed on the commands nature.
- **Tooltip Manager**: Sets up and updates tooltips for each tray icon.
- **Platform-Specific Integration**: This will be where platform-specific code, such as using macOS APIs<br/>for creating and managing tray icons. Conditional Compilation can be used here to support different<br/> operating systems.

### Menu Builder Module

Constructs the dropdown menu for each tray icon, based on the commands defined in the configuration.<br/>It determines the order of menu items according to the `ordering_strategy`.

- **Menu Item Creator**: Creates individual menu items for commands, setting their labels, actions, and shortcuts as defined in the configuration.
- **Menu Structure Organizer**: Organizes the menu items according to the specified ordering strategy (`linear`, `numeral`, `alphabetical`).
- **Submemu Support**: (Optional) if the application needs to support nested menus, this component can handle the creation and management of submenus.

### Plarform Abstraction Module

Provides an abstraction layer for the plaform-specific code, ensure `gensystray`logic remains platform agnostic. This module encapsulates the differences between operating systems and exposes a common interface for the rest of the program.

**Target Platforms**:
- **MacOS Sequoia**.
- **Linux Distributions** (Debian, Ubuntu, ArchLinux).

*Contains MacOS-specific code for creating and managing system tray icons, notifications, and other OS-level integrations.*

