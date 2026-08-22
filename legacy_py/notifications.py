"""
KDE notification helper.

We use the org.freedesktop.Notifications D-Bus interface (the same surface KDE
Plasma uses for ``kdialog --passivepopup`` and the system notification area).
When D-Bus is unreachable (e.g. headless smoke tests) we fall back to a
no-op so callers never crash.
"""
from __future__ import annotations

import os
from typing import Optional


APP_NAME = "DSH Desktop"
APP_ID = "dsh.desktop"
REPLACES_ID = "dsh-desktop"


def _bus():
    try:
        import dbus  # type: ignore
        return dbus.SessionBus()
    except Exception:
        return None


def _iface(bus):
    if bus is None:
        return None
    try:
        proxy = bus.get_object("org.freedesktop.Notifications",
                               "/org/freedesktop/Notifications")
        return proxy.get_interface("org.freedesktop.Notifications")
    except Exception:
        return None


def notify(summary: str, body: str = "", *, urgency: str = "normal",
           icon: str = "dialog-information", timeout_ms: int = 5000,
           actions: Optional[list[tuple[str, str]]] = None) -> int:
    """Show a desktop notification; return the notification id (0 on failure)."""
    bus = _bus()
    iface = _iface(bus)
    if iface is None:
        return 0
    urgency_map = {"low": 0, "normal": 1, "critical": 2}
    u = urgency_map.get(urgency, 1)
    try:
        hints = {
            "desktop-entry": dbus.String(APP_ID),  # type: ignore[name-defined]
            "urgency": dbus.Byte(u),  # type: ignore[name-defined]
        }
    except Exception:
        hints = {"urgency": u}
    if actions is None:
        actions = []
    try:
        nid = iface.Notify(
            APP_NAME,
            0,                  # replaces_id
            icon,
            summary,
            body,
            actions,
            hints,
            max(1000, timeout_ms),
        )
        return int(nid)
    except Exception:
        return 0


def notify_progress(summary: str, body: str, percent: int) -> int:
    """Show a progress notification; returns its id so the caller can update it."""
    bus = _bus()
    iface = _iface(bus)
    if iface is None:
        return 0
    try:
        import dbus  # type: ignore
        hints = {
            "desktop-entry": dbus.String(APP_ID),  # type: ignore
            "urgency": dbus.Byte(1),  # type: ignore
            "value": dbus.Int32(max(0, min(100, int(percent)))),  # type: ignore
        }
        return int(iface.Notify(APP_NAME, 0, "dialog-information", summary, body, [], hints, 0))
    except Exception:
        return 0


def close(nid: int) -> None:
    if nid <= 0:
        return
    bus = _bus()
    iface = _iface(bus)
    if iface is None:
        return
    try:
        iface.CloseNotification(dbus.UInt32(nid))  # type: ignore[name-defined]
    except Exception:
        pass
