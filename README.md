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
- Theme-aware tray, window, taskbar, and dialog icons; the black/white whale
  pair is the SVG-only canonical source in `assets/` (embedded via
  `assets/icons.qrc`, no PNG variants).
- Unified, single-action update for both the DSH backend (`dsh` CLI / npm)
  and the desktop build (project Gitee releases), run from one native dialog
  and a dedicated `dsh-desktop-updater` helper.
- Read-only `dsh-web.service` discovery that validates and reuses an existing
  official unit, with a supervised `dsh web` fallback and consent before
  starting an existing inactive or failed official service.
- Asynchronous backend health monitoring and update checks.

## Requirements

- Arch Linux or a derivative
- KDE Plasma 6 (other Linux desktops may have reduced integration)
- Qt 6.5 or newer: Core, GUI, Widgets, Network, D-Bus, SVG, and WebEngine.
  Chromium's web-page dark forcing (`QWebEngineSettings::ForceDarkMode`)
  exists only from Qt 6.7; it is guarded by a compile-time version check, so
  on Qt 6.5 / 6.6 the desktop degrades gracefully instead of forcing inverted
  web rendering.
- CMake, Ninja, libxcb, polkit, Node.js/npm, and an installed `dsh` CLI

## Install

```sh
sudo packaging/install.sh
```

The installer builds the C++ application, installs it under `/usr`, registers
the desktop/autostart entries and theme-aware icons, and configures the theme
export service. It detects and reuses an existing `dsh-web.service`; the
decision to start an existing inactive or failed official service is deferred
to the desktop, which asks the user for consent at runtime rather than
blindly starting a second `dsh web`.

## Backend management

For loopback URLs, DSH Desktop runs a read-only discovery pass over the
system and current-user `dsh-web.service` units. A candidate is only reused
when it loads (`LoadState=loaded`) and its `ExecStart` invokes the official
`dsh web`; when both scopes are valid it prefers the current user's user-level
unit. If no valid unit exists it starts and supervises `dsh web` directly.
Explicit remote URLs use external mode and are never started, stopped, or
restarted by the desktop application.

An existing *inactive* or *failed* official service is not started
unconditionally: the desktop shows a native consent prompt and only starts it
after the user confirms. The ready-made service pieces (read-only discovery,
origin recording, the detect→reuse→provision decision model) are implemented;
installer-driven provisioning and the richer unified service-manager UI remain
planned (see
[docs/DSH-DESKTOP-SERVICE-PLAN.zh.md](docs/DSH-DESKTOP-SERVICE-PLAN.zh.md)).

The quit dialog only stops a managed backend when the user selects that
option. Closing the main window hides it to the tray.

## Updates

An automatic check runs 60 seconds after launch, and the tray's "Check for
updates" action runs an on-demand check. A background worker checks both the
installed `dsh` CLI against the npm registry and the desktop build against the
project's Gitee releases. The two results are merged into a single plan shown
as one "Update to latest" tray action; the native update dialog lists both
components, defaults to every updateable one (backend first), and runs them
serially with an indeterminate (busy) progress bar.

The backend component is upgraded through the existing asynchronous
polkit-aware updater; the desktop component downloads its selected asset,
verifies its SHA-256, and hands off to `dsh-desktop-updater`, which replaces
the running binary in place (with a `--pid` / `--source` / `--destination` /
`--sha256` / `--install-prefix` contract) without stopping the backend.

## Build and test

```sh
# Development: RelWithDebInfo plus the full test suite
cmake --preset dev
cmake --build build/dev --parallel
ctest --test-dir build/dev --output-on-failure

# Packaging and deployment: Release with a /usr install prefix
cmake --preset release
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
```

The shared presets only generate `build/dev/` and `build/release/`. Put local
overrides in the ignored `CMakeUserPresets.json` instead of creating additional
`cmake-build-*` trees.

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
