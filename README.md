# DSH Desktop — Native Linux/KDE Plasma 6 wrapper for DeepSeek Harness

DSH Desktop is a C++17/Qt 6 system-tray application for the official
[DeepSeek Harness](https://github.com/deepseek-ai/deepseek-harness) web UI.
It embeds the UI with Qt WebEngine and integrates with KDE Plasma without
Electron, Tauri, or a Python runtime.

## Features

- Persistent `QSystemTrayIcon` menu and native Qt dialogs.
- Persistent Qt WebEngine profile for login sessions, cache, and cookies.
- Authenticated session-export downloads handled by WebEngine with a native
  save dialog, progress display, and desktop notification.
- External HTTP(S) links opened in the system browser.
- Theme-aware tray, window, taskbar, and dialog icons.
- Asynchronous backend health monitoring and update checks.
- Systemd service management with a supervised `dsh web` fallback.

## Requirements

- Arch Linux or a derivative
- KDE Plasma 6 (other Linux desktops may have reduced integration)
- Qt 6.5 or newer: Core, GUI, Widgets, Network, D-Bus, SVG, and WebEngine
- CMake, Ninja, libxcb, polkit, Node.js/npm, and an installed `dsh` CLI

## Install

```sh
sudo packaging/install.sh
```

The installer builds the C++ application, installs it under `/usr`, registers
the desktop/autostart entries and theme-aware icons, configures the theme
export service, and starts an existing `dsh-web.service` when available.

## Backend management

For loopback URLs, DSH Desktop first uses an installed system or user
`dsh-web.service`. If no unit exists, it starts and supervises `dsh web`
directly. Explicit remote URLs use external mode and are never started,
stopped, or restarted by the desktop application.

The quit dialog only stops a managed backend when the user selects that
option. Closing the main window hides it to the tray.

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Useful diagnostic modes:

```sh
dsh-desktop --help
dsh-desktop --probe
dsh-desktop --smoke
dsh-desktop --self-test
```

`--help`, `--version`, `--probe`, and `--smoke` work without a graphical
display. `--self-test` automatically uses the offscreen Qt platform unless an
explicit platform is configured.

Runtime logs are written to the Qt `AppDataLocation` by default. Use
`--log-file <path>` to override the destination.

For detailed Chinese documentation, see [README.zh.md](README.zh.md).

## Uninstall

```sh
sudo packaging/install.sh --uninstall
```

User configuration, WebEngine profile data, and downloaded exports are kept
unless removed manually.

## License

MIT
