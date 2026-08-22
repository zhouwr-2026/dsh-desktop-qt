"""
Icon asset resolver.

The official DeepSeek Harness whale logo is shipped as a black SVG (assets/dsh-whale-black.svg)
and a derived white SVG (assets/dsh-whale-white.svg). PNG variants for KDE Plasma 6
tray, window and taskbar are pre-rasterised under assets/icons/ at standard sizes.

This module picks the right variant based on the current colour scheme, and exposes
both ``QIcon`` (for window/taskbar) and ``QPixmap`` (for tray) representations.
"""
from __future__ import annotations

import os
from pathlib import Path
from typing import Optional

from PyQt6.QtCore import QSize, Qt
from PyQt6.QtGui import QIcon, QPixmap


def _candidate_asset_dirs() -> list[Path]:
    """Return candidate asset directories in priority order.

    Order:
      1. Beside the package source (developer / editable install).
      2. System data dir (pip-installed wheel puts assets under
         ``share/dsh-desktop/assets``).
      3. XDG data dirs as a last-resort fallback.
    """
    repo = Path(__file__).resolve().parent.parent / "assets"
    here = Path(__file__).resolve().parent / "assets"
    xdg = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))) / "dsh-desktop" / "assets"
    system = Path("/usr/share/dsh-desktop/assets")
    return [p for p in (repo, here, system, xdg) if p.exists()]


def _assets_root() -> Path:
    """First candidate that actually contains the SVG logo."""
    for p in _candidate_asset_dirs():
        if (p / "dsh-whale-black.svg").exists():
            return p
    # Fallback to the first directory so missing-asset paths degrade to empty
    # icons rather than crashing.
    return _candidate_asset_dirs()[0] if _candidate_asset_dirs() else Path("assets")


ASSETS_DIR = _assets_root()
ICONS_DIR = ASSETS_DIR / "icons"


def icon_for_scheme(scheme: str) -> QIcon:
    """Return a multi-resolution ``QIcon`` for the given scheme ('light'|'dark')."""
    color = "white" if scheme == "dark" else "black"
    icon = QIcon()
    # Load in priority order; KDE picks the size that matches the surface.
    sizes = (16, 22, 32, 48, 64, 128, 256)
    for size in sizes:
        path = ICONS_DIR / f"dsh-whale-{color}-{size}.png"
        if path.exists():
            icon.addFile(str(path), QSize(size, size))
    # SVG fallback for surfaces that ask for non-pre-rasterised sizes (rare on Plasma).
    svg = ASSETS_DIR / f"dsh-whale-{color}.svg"
    if svg.exists():
        icon.addFile(str(svg), QSize(512, 512))
    return icon


def pixmap_for_scheme(scheme: str, size: int = 64) -> QPixmap:
    """Return a single ``QPixmap`` for the given scheme at the requested size."""
    color = "white" if scheme == "dark" else "black"
    png = ICONS_DIR / f"dsh-whale-{color}-{size}.png"
    if png.exists():
        pix = QPixmap(str(png))
        if not pix.isNull():
            return pix
    # Fall back to SVG render.
    svg = ASSETS_DIR / f"dsh-whale-{color}.svg"
    if svg.exists():
        pix = QPixmap(str(svg))
        if not pix.isNull():
            return pix.scaled(
                size, size,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
    return QPixmap()


def tray_pixmap(scheme: str) -> QPixmap:
    """KDE Plasma 6's StatusNotifierItem expects ~22-32px pixmaps for the tray."""
    return pixmap_for_scheme(scheme, size=64)


def app_icon(scheme: str = "light") -> QIcon:
    return icon_for_scheme(scheme)
