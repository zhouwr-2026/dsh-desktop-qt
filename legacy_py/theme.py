"""
Theme detection for DSH Desktop.

Order of preference:
  1. Qt 6.5+ `QStyleHints.colorScheme()` — set automatically by KDE Plasma 6
     from the `org.freedesktop.appearance` colour-scheme setting.
  2. Direct KDE config probe (`~/.config/kdeglobals` + Plasma 6's
     `~/.config/plasmarc`) for `ColorScheme`/`Name` keys.
  3. org.freedesktop.portal.Settings via D-Bus (works in sandboxed Flatpak too).
  4. Hard-coded fallback (light) so the app never crashes during startup.

Emits a Qt signal whenever the scheme flips so the tray icon, window icon and
taskbar group can re-bind to the matching colour variant.
"""
from __future__ import annotations

import configparser
import os
from pathlib import Path
from typing import Callable, Optional

from PyQt6.QtCore import QObject, QTimer, pyqtSignal


def _qt_color_to_name(scheme) -> str:
    # Qt6 enums: Qt.ColorScheme.Light / Dark / Unknown
    name = getattr(scheme, "name", None) or str(scheme)
    return name.lower().replace("qt.colorscheme.", "")


class ThemeWatcher(QObject):
    """Polls the system for dark/light transitions and emits ``changed``."""

    changed = pyqtSignal(str)  # emits "dark" or "light"

    POLL_MS = 4000

    def __init__(self, parent: Optional[QObject] = None) -> None:
        super().__init__(parent)
        self._current: str = "light"
        self._timer = QTimer(self)
        self._timer.setInterval(self.POLL_MS)
        self._timer.timeout.connect(self._tick)
        # Initial best-effort sync so the first emit is the real value.
        try:
            self._current = self._probe()
        except Exception:
            pass

    def start(self) -> None:
        self._timer.start()

    def stop(self) -> None:
        self._timer.stop()

    @property
    def current(self) -> str:
        return self._current

    def _tick(self) -> None:
        new_value = self._probe()
        if new_value != self._current:
            self._current = new_value
            self.changed.emit(new_value)

    # ----- detection -----

    def _probe(self) -> str:
        # 1. Qt's QStyleHints (KDE Plasma 6 forwards its appearance setting into Qt).
        try:
            from PyQt6.QtGui import QGuiApplication
            app = QGuiApplication.instance()
            if app is not None:
                scheme = app.styleHints().colorScheme()
                name = _qt_color_to_name(scheme)
                if name in ("dark", "light"):
                    return name
                # Unknown -> fall through to portal/config probes.
        except Exception:
            pass

        # 2. KDE config files.
        kde_value = self._read_kde_config()
        if kde_value in ("dark", "light"):
            return kde_value

        # 3. xdg-desktop-portal settings via D-Bus (rare, but useful in sandboxes).
        portal_value = self._read_portal()
        if portal_value in ("dark", "light"):
            return portal_value

        return self._current or "light"

    @staticmethod
    def _read_kde_config() -> Optional[str]:
        cfg_paths = [
            Path.home() / ".config" / "kdeglobals",
            Path.home() / ".config" / "plasmarc",
            Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "kdeglobals",
        ]
        for cfg_path in cfg_paths:
            if not cfg_path.exists():
                continue
            try:
                parser = configparser.ConfigParser(strict=False)
                parser.read(cfg_path, encoding="utf-8")
            except (configparser.Error, OSError):
                continue

            # Plasma 6: [General] ColorScheme=BreezeClassicDark  OR  ColorScheme=Light/Dark
            for section in ("General", "Appearance", "KDE"):
                if not parser.has_section(section):
                    continue
                cs = parser.get(section, "ColorScheme", fallback=None)
                if cs:
                    cs_l = cs.lower()
                    if "dark" in cs_l:
                        return "dark"
                    if "light" in cs_l:
                        return "light"
            # Plasma 6 LookAndFeelPackage sometimes encodes the theme.
            for section in ("General",):
                if not parser.has_section(section):
                    continue
                look = parser.get(section, "LookAndFeelPackage", fallback=None)
                if look:
                    l = look.lower()
                    if "dark" in l:
                        return "dark"
                    if "light" in l or "breeze" in l and "dark" not in l:
                        return "light"
        return None

    @staticmethod
    def _read_portal() -> Optional[str]:
        try:
            import dbus  # type: ignore
        except ImportError:
            return None
        try:
            bus = dbus.SessionBus()
            proxy = bus.get_object("org.freedesktop.portal.Desktop",
                                   "/org/freedesktop/portal/desktop")
            iface = dbus.Interface(proxy, "org.freedesktop.portal.Settings")
            value = iface.Read("org.freedesktop.appearance", "color-scheme")
            if value == 1:
                return "dark"
            if value == 2:
                return "light"
        except Exception:
            return None
        return None


def is_dark_qpalette() -> bool:
    """Quick fallback using QPalette luminance."""
    try:
        from PyQt6.QtGui import QGuiApplication
        from PyQt6.QtGui import QPalette
    except ImportError:
        return False
    app = QGuiApplication.instance()
    if app is None:
        return False
    palette = app.palette()
    window = palette.color(QPalette.ColorRole.Window)
    # Rec. 709 luminance.
    luminance = 0.2126 * window.redF() + 0.7152 * window.greenF() + 0.0722 * window.blueF()
    return luminance < 0.5
