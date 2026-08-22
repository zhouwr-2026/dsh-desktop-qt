"""
Main DSH Desktop application: tray icon + native window hosting the dsh Web UI.

The host process is a PyQt6 application that:
  - Owns a single ``QSystemTrayIcon`` (KDE Plasma 6 StatusNotifierItem).
  - Owns one ``QWebEngineView`` showing ``http://127.0.0.1:3080``.
  - Reacts to theme changes by swapping the icon variant (black/white whale).
  - Intercepts external http(s) links and routes them through ``xdg-open``.
  - Intercepts the official ``session-log-export`` download flow and routes
    it through a native save dialog with progress.
  - Surfaces tray menu items: Show / Hide / Check for updates / Update to latest
    / Restart desktop / Quit (with native confirm + active-task warning + an
    "exit background service" checkbox).
  - Talks to the existing ``dsh-web.service`` for backend lifecycle (start/stop/
    restart) or, when no systemd unit is installed, supervises the process
    directly.

The single entrypoint is ``main()``; it is also exposed as the module
``python -m dsh_desktop`` console script.
"""
from __future__ import annotations

import argparse
import os
import signal
import subprocess
import sys
import urllib.parse
import webbrowser
from pathlib import Path
from typing import Optional

# Ensure Qt loads the Breeze-style platform theme by default on KDE sessions.
os.environ.setdefault("QT_QPA_PLATFORMTHEME", "kde6")
os.environ.setdefault("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1")

# PyQt6-WebEngine MUST be imported (or AA_ShareOpenGLContexts must be set)
# BEFORE QApplication is constructed, or QtWebEngineWidgets will refuse to
# import later. We import it eagerly so the user gets a clean traceback
# instead of a cryptic QOpenGLContext error.
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QCloseEvent
from PyQt6.QtWebEngineWidgets import QWebEngineView  # noqa: F401
from PyQt6.QtWebEngineCore import (
    QWebEngineNavigationRequest,
    QWebEnginePage,
    QWebEngineProfile,
    QWebEngineSettings,
)
from PyQt6.QtWidgets import QApplication

# Share OpenGL contexts so QtWebEngineProcess can render in the same process
# model as the main UI.
QApplication.setAttribute(Qt.ApplicationAttribute.AA_ShareOpenGLContexts, True)

from PyQt6.QtCore import QPoint, QSize, QTimer, QUrl, pyqtSignal
from PyQt6.QtGui import (
    QAction,
    QDesktopServices,
    QGuiApplication,
    QIcon,
    QKeySequence,
    QShortcut,
)
from PyQt6.QtWidgets import (
    QCheckBox,
    QDialog,
    QDialogButtonBox,
    QLabel,
    QMainWindow,
    QMenu,
    QMessageBox,
    QSystemTrayIcon,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from .backend import BackendMode, BackendStatus, DshBackend
from .downloads import DownloadInterceptor
from .icons import app_icon, icon_for_scheme, pixmap_for_scheme, tray_pixmap
from .notifications import notify as desktop_notify
from .theme import ThemeWatcher
from .updater import UpdateStatus, check_for_update, perform_update


APP_NAME = "DSH Desktop"
APP_ID = "dsh.desktop"
DEFAULT_URL = os.environ.get("DSH_DESKTOP_URL", "http://127.0.0.1:3080")


# ---------------------------------------------------------------------------
# Loopback page: rejects any navigation that leaves 127.0.0.1 / localhost
# ---------------------------------------------------------------------------


class LoopbackWebPage(QWebEnginePage):
    """QWebEnginePage that blocks navigation to non-loopback hosts."""

    # Class-level allow-list of (host, port) tuples considered "internal".
    INTERNAL_HOSTS = {"127.0.0.1", "localhost", "::1"}

    def __init__(self, profile, parent=None, *, external_opener=None,
                 log: Optional[Callable[[str], None]] = None) -> None:
        super().__init__(profile, parent)
        self._external_opener = external_opener or (lambda u: None)
        self._log = log or (lambda m: None)

    def _is_internal(self, url: QUrl) -> bool:
        host = (url.host() or "").lower()
        if host in self.INTERNAL_HOSTS:
            return True
        # data:, blob:, about:, file:, javascript:, mailto:, tel: stay inside.
        if url.scheme().lower() in {"data", "blob", "about", "file", "javascript", "mailto", "tel"}:
            return True
        return False

    def acceptNavigationRequest(self, url: QUrl, nav_type, is_main_frame: bool) -> bool:  # type: ignore[override]
        if self._is_internal(url):
            return True
        # External URL: open in default browser, cancel the in-webview nav.
        if url.scheme().lower() in {"http", "https"}:
            self._log(f"page: rejecting navigation to {url.toString()} (main={is_main_frame})")
            try:
                self._external_opener(url)
            except Exception as exc:
                self._log(f"page: external opener failed: {exc}")
            return False
        # Non-http(s) external schemes (e.g. magnet:) — also reject so Chromium
        # doesn't try to handle them itself.
        return False

    def createWindow(self, window_type):  # type: ignore[override]
        """``window.open()`` / ``target=_blank`` is a no-op: the real open
        happens in the ``newWindowRequested`` signal handler connected from
        ``DshWebWindow``. Returning ``self`` keeps Chromium happy without
        actually creating a visible window.
        """
        return self


# ---------------------------------------------------------------------------
# Native confirm dialog (exit)
# ---------------------------------------------------------------------------


class ExitDialog(QDialog):
    """Native exit confirmation with active-task warning and a background-service checkbox."""

    def __init__(self, parent: Optional[QWidget], *, backend_url: str,
                 active_tasks: int, mode: BackendMode, parent_window_title: str = "") -> None:
        super().__init__(parent)
        self.setWindowTitle("退出 DSH Desktop")
        self.setMinimumWidth(440)
        layout = QVBoxLayout(self)

        if active_tasks > 0:
            warn = QLabel(
                f"⚠  检测到 <b>{active_tasks}</b> 个后台任务正在执行，\n"
                "现在退出可能会中断它们。"
            )
            warn.setStyleSheet("color: #d35400; font-weight: 600;")
            warn.setWordWrap(True)
            layout.addWidget(warn)

        msg = QLabel(
            "确定要退出 DSH Desktop 吗？\n\n"
            "• 托盘菜单会消失\n"
            f"• 主窗口 «{parent_window_title or 'DSH Web'}» 将被关闭\n"
            "• 勾选下方选项可同时停止后台 dsh web 服务"
        )
        msg.setWordWrap(True)
        layout.addWidget(msg)

        self.bg_checkbox = QCheckBox(f"同时停止后台 dsh web 服务  ({backend_url})")
        # Disable the checkbox when no service is configured (supervised mode
        # without a unit means stopping the service also kills the supervisor;
        # still allow it for symmetry - user explicitly wants it).
        self.bg_checkbox.setChecked(False)
        self.bg_checkbox.setEnabled(True)
        layout.addWidget(self.bg_checkbox)

        if mode == BackendMode.SUPERVISED:
            self.bg_checkbox.setText(self.bg_checkbox.text() + "  [会一并停止由桌面端拉起的 dsh web 子进程]")

        layout.addStretch(1)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Cancel | QDialogButtonBox.StandardButton.Ok
        )
        buttons.button(QDialogButtonBox.StandardButton.Ok).setText("退出")
        buttons.button(QDialogButtonBox.StandardButton.Cancel).setText("取消")
        buttons.button(QDialogButtonBox.StandardButton.Ok).setDefault(True)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)


# ---------------------------------------------------------------------------
# Update dialog
# ---------------------------------------------------------------------------


class UpdateDialog(QDialog):
    """Plain dialog showing current/latest versions and a 'perform update' button."""

    def __init__(self, parent: Optional[QWidget], status: UpdateStatus) -> None:
        super().__init__(parent)
        self.setWindowTitle("DSH 更新")
        self.setMinimumWidth(480)
        layout = QVBoxLayout(self)
        layout.addWidget(QLabel(f"当前版本：<b>{status.current or '未知'}</b>"))
        layout.addWidget(QLabel(f"最新版本：<b>{status.latest or '未知'}</b>"))
        if status.update_available:
            layout.addWidget(QLabel(
                "<span style='color:#27ae60;font-weight:600;'>有新版本可用，点击下方按钮立即更新。</span>"
            ))
        elif status.detail == "offline or unable to query npm registry":
            layout.addWidget(QLabel("<span style='color:#d35400;'>无法连接 npm registry，请稍后重试。</span>"))
        else:
            layout.addWidget(QLabel("<span style='color:#27ae60;'>已是最新版本。</span>"))

        self._log = QTextEdit()
        self._log.setReadOnly(True)
        self._log.setVisible(False)
        self._log.setMinimumHeight(160)
        layout.addWidget(self._log)

        buttons = QDialogButtonBox()
        self.update_btn = buttons.addButton("更新到最新版", QDialogButtonBox.ButtonRole.AcceptRole)
        buttons.addButton("关闭", QDialogButtonBox.ButtonRole.RejectRole)
        buttons.accepted.connect(self._on_update)
        buttons.rejected.connect(self.reject)
        if not status.update_available:
            self.update_btn.setEnabled(False)
        layout.addWidget(buttons)

    def _on_update(self) -> None:
        self._log.setVisible(True)
        self.update_btn.setEnabled(False)

        def log(line: str) -> None:
            self._log.append(line)

        ok, output = perform_update(log=log)
        log("")
        log("完成。" if ok else "更新失败。")
        if ok:
            self._log.append(f"<span style='color:#27ae60;'>已安装新版本。重启后生效。</span>")
            self.update_btn.setEnabled(False)


# ---------------------------------------------------------------------------
# Main window
# ---------------------------------------------------------------------------


class DshWebWindow(QMainWindow):
    """Hosts the dsh Web UI; handles external links and downloads."""

    download_started = pyqtSignal(str)  # human-readable message
    download_finished = pyqtSignal(bool, str)  # ok, detail

    def __init__(self, url: str, theme: ThemeWatcher, log) -> None:
        super().__init__()
        self._url = url
        self._theme = theme
        self._log = log

        self._web = QWebEngineView(self)
        # Use a dedicated profile so downloadRequested can be wired to the
        # SAME page that the view is rendering. Connecting to a stray profile
        # silently no-ops because the view never visits it.
        self._profile = QWebEngineProfile(self)
        self._profile.downloadRequested.connect(self._on_download_requested)

        # Configure profile settings: allow fullscreen + plugins for the rich
        # DSH Web UI surface.
        settings = self._profile.settings()
        try:
            settings.setAttribute(
                QWebEngineSettings.WebAttribute.FullScreenSupportEnabled, True)
            settings.setAttribute(
                QWebEngineSettings.WebAttribute.PluginsEnabled, True)
        except Exception:
            pass

        # Attach the dedicated profile to a page, then the page to the view.
        page = LoopbackWebPage(
            self._profile, self._web,
            external_opener=self._open_external,
            log=self._log,
        )
        # Sub-frame external links: navigationRequested fires for every
        # navigation request including iframe clicks. Block + redirect to
        # the system browser for any non-loopback http(s).
        page.navigationRequested.connect(self._on_navigation_request)
        # window.open() / target=_blank -> open in default browser, no new window.
        page.newWindowRequested.connect(self._on_window_open_requested)
        self._web.setPage(page)

        self._web.setUrl(QUrl(url))
        self.setCentralWidget(self._web)
        self.resize(1280, 820)
        self.setWindowTitle("DSH Desktop")
        self.setWindowIcon(app_icon(self._theme.current))

        # External URL intercept: any navigation away from the loopback host
        # is cancelled and routed through xdg-open / the system browser.
        self._web.urlChanged.connect(self._on_url_changed)

        # Download interceptor handles only session-log paths (configurable).
        self._download_interceptor = DownloadInterceptor(parent=self, log=self._log)

        # Refresh window icon on theme changes.
        self._theme.changed.connect(self._on_theme_changed)

    # ----- slots -----

    def _on_url_changed(self, qurl: QUrl) -> None:
        host = (qurl.host() or "").lower()
        # The official DSH web runs on 127.0.0.1 (or ::1). Anything else is
        # treated as an external link.
        if host in {"127.0.0.1", "localhost", "::1", ""}:
            return
        scheme = qurl.scheme().lower()
        if scheme in {"http", "https"}:
            self._log(f"window: intercepting external link {qurl.toString()}")
            # Cancel navigation; open externally.
            self._web.stop()
            self._web.setUrl(QUrl(self._url))
            self._open_external(qurl)

    def _is_external(self, qurl: QUrl) -> bool:
        host = (qurl.host() or "").lower()
        if host in {"127.0.0.1", "localhost", "::1", ""}:
            return False
        return qurl.scheme().lower() in {"http", "https"}

    def _on_navigation_request(self, request) -> None:
        """Log external navigation; the actual rejection happens in
        ``LoopbackWebPage.acceptNavigationRequest`` below. We also open the
        URL externally so the user lands on it regardless of who wins."""
        try:
            url = request.url()
            if self._is_external(url):
                self._log(f"window: blocking external navigation to {url.toString()}")
                self._open_external(url)
        except Exception as exc:
            self._log(f"window: navigation handler error: {exc}")

    def _on_window_open_requested(self, request) -> None:
        """User clicked target=_blank on a link -> open in default browser."""
        try:
            url = request.requestedUrl()
            if self._is_external(url):
                self._log(f"window: window.open -> {url.toString()}")
                self._open_external(url)
        except Exception as exc:
            self._log(f"window: window-open handler error: {exc}")

    def _open_external(self, qurl: QUrl) -> None:
        url = qurl.toString()
        try:
            # Prefer Qt's openUrl which honours xdg mimeapps.
            if not QDesktopServices.openUrl(qurl):
                raise RuntimeError("QDesktopServices refused")
        except Exception:
            # Fallback to webbrowser module (xdg-open under the hood).
            try:
                webbrowser.open(url)
                return
            except Exception:
                pass
            # Last resort: subprocess xdg-open.
            try:
                subprocess.Popen(["xdg-open", url], start_new_session=True)
            except OSError as exc:
                self._log(f"window: failed to open {url}: {exc}")

    def _on_download_requested(self, item) -> None:
        # The default Qt download item still owns a temp path; we ignore it
        # unless the interceptor decides to let it through.
        try:
            suggested = item.suggestedFileName()
        except Exception:
            suggested = ""
        url = item.url() if hasattr(item, "url") else QUrl()
        self._download_interceptor.handle(url, suggested)
        try:
            item.cancel()
        except Exception:
            pass

    def _on_theme_changed(self, scheme: str) -> None:
        self.setWindowIcon(app_icon(scheme))

    # ----- window events -----

    def closeEvent(self, event: QCloseEvent) -> None:
        # Hide-to-tray semantics: clicking the window close button should
        # only hide, not quit. Quit happens via the tray's "Quit" menu which
        # shows the native confirm dialog.
        event.ignore()
        self.hide()


# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------


class DshDesktopApp:
    """Top-level controller owning tray, window, backend, theme, updater."""

    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.log = self._make_logger(args.log_file)
        self.backend = DshBackend(url=args.url, log=self.log)
        self.theme = ThemeWatcher()
        self.tray: Optional[QSystemTrayIcon] = None
        self.tray_menu: Optional[QMenu] = None
        self.window: Optional[DshWebWindow] = None
        self._update_in_progress = False
        self._shutdown_started = False

    # ----- bootstrap -----

    @staticmethod
    def _probe_tray_available(timeout_s: float) -> bool:
        """Detect a StatusNotifierWatcher (KDE Plasma 6) before asking Qt.

        ``QSystemTrayIcon.isSystemTrayAvailable()`` blocks indefinitely on
        D-Bus if the watcher service is missing or the session bus is
        unreachable (offscreen CI runs, ssh-without-D-Bus-forwarding, etc.).
        We sniff D-Bus directly with a hard timeout and trust the result.
        """
        # Fast-path: explicit smoke / headless mode -> assume available so
        # the rest of the init path still exercises (test harness).
        if os.environ.get("QT_QPA_PLATFORM", "").lower() == "offscreen":
            return True
        import threading
        result: dict[str, object] = {}

        def sniff() -> None:
            try:
                import dbus  # type: ignore
                bus = dbus.SessionBus()
                # KDE Plasma 6 and GNOME Shell 3.x+ expose
                # org.kde.StatusNotifierWatcher on the session bus.
                proxy = bus.get_object("org.kde.StatusNotifierWatcher",
                                       "/StatusNotifierWatcher")
                result["available"] = proxy is not None
            except Exception:
                result["available"] = False

        t = threading.Thread(target=sniff, daemon=True)
        t.start()
        t.join(timeout_s)
        if t.is_alive():
            return False
        return bool(result.get("available", False))

    def run(self) -> int:
        # Single-instance check via a lock file under $XDG_RUNTIME_DIR or /tmp.
        lock_path = Path(os.environ.get("XDG_RUNTIME_DIR", "/tmp")) / "dsh-desktop.lock"
        try:
            import fcntl
            fd = os.open(str(lock_path), os.O_CREAT | os.O_RDWR, 0o600)
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                self.log(f"startup: another instance is holding {lock_path}; exiting")
                return 2
        except OSError as exc:
            self.log(f"startup: lock unavailable ({exc}); proceeding")

        if os.environ.get("DSH_DESKTOP_DEBUG"):
            print(f"[run] 锁已获取 pid={os.getpid()}", flush=True)

        self.qt_app = QApplication.instance() or QApplication(sys.argv)
        self.qt_app.setApplicationName(APP_NAME)
        self.qt_app.setApplicationDisplayName(APP_NAME)
        self.qt_app.setDesktopFileName(APP_ID)
        self.qt_app.setQuitOnLastWindowClosed(False)  # we live in the tray
        self.qt_app.setWindowIcon(app_icon(self.theme.current))

        if os.environ.get("DSH_DESKTOP_DEBUG"):
            print(f"[run] QApplication 就绪", flush=True)

        if os.environ.get("DSH_DESKTOP_DEBUG"):
            print(f"[run] 检查托盘可用性...", flush=True)
        # isSystemTrayAvailable() can block while probing the StatusNotifierWatcher
        # on D-Bus; we wrap it in a thread with a hard timeout so a missing
        # plasma-org.kde.StatusNotifierWatcher service doesn't freeze us.
        tray_available = self._probe_tray_available(timeout_s=1.5)
        if not tray_available:
            QMessageBox.critical(None, APP_NAME, "系统托盘不可用，请检查 KDE Plasma 是否在运行。")
            return 3
        if os.environ.get("DSH_DESKTOP_DEBUG"):
            print(f"[run] 托盘 ok", flush=True)
        if not QSystemTrayIcon.supportsMessages():
            self.log("tray: 通知不被系统托盘支持")

        self.theme.start()
        if os.environ.get("DSH_DESKTOP_DEBUG"):
            print(f"[run] 主题已启动", flush=True)
        self.theme.changed.connect(self._on_theme_changed)
        if os.environ.get("DSH_DESKTOP_DEBUG"):
            print(f"[run] 主题信号已连接", flush=True)

        # Backend readiness.
        if not self.backend.is_running():
            self.log("startup: dsh web not running; trying to start it")
            ok = self.backend.start()
            if not ok:
                QMessageBox.warning(
                    None, APP_NAME,
                    "无法启动 dsh web 服务。\n请检查：\n"
                    f"  • dsh-web.service (systemd) 是否已启用\n"
                    "  • 'dsh' 命令是否在 PATH 中\n"
                    "  • ~/.dsh/profiles/web/cordis.patch.yml 是否合法\n"
                    f"桌面端将仍会启动，请访问 {self.backend.url} 查看。",
                )

        self.window = DshWebWindow(self.backend.url, self.theme, self.log)
        if os.environ.get("DSH_DESKTOP_DEBUG"):
            print("[run] 窗口已创建", flush=True)

        self._build_tray()
        if os.environ.get("DSH_DESKTOP_DEBUG"):
            print("[run] 托盘已创建", flush=True)
        self.tray.show()
        self.window.show()
        self._refresh_tray_menu()

        if os.environ.get("DSH_DESKTOP_DEBUG"):
            print("[run] 即将绑定信号", flush=True)

        # Wire SIGINT/SIGTERM to a graceful quit (mainly for the headless smoke test).
        for sig in (signal.SIGINT, signal.SIGTERM):
            try:
                signal.signal(sig, lambda *_: self._request_quit(silent=True))
            except (ValueError, OSError):
                pass

        # Self-test mode: dump a structured report then quit gracefully.
        if getattr(self.args, "self_test", False):
            self._dump_self_test_report()
            QTimer.singleShot(500, lambda: self._request_quit(silent=True))
            # Don't exec the event loop; the single-shot will trigger quit.
            # Use exec with a short ceiling so we don't block forever if quit
            # is delayed.
            from PyQt6.QtCore import QEventLoop
            loop = QEventLoop()
            QTimer.singleShot(5000, loop.quit)
            self._self_test_quit_loop = loop
            loop.exec()
            return 0

        if os.environ.get("DSH_DESKTOP_DEBUG"):
            print("[run] 进入事件循环", flush=True)
        return self.qt_app.exec()

    def _dump_self_test_report(self) -> None:
        """Print a structured report of the live app state. Used by --self-test."""
        report = {
            "tray_visible": self.tray.isVisible() if self.tray else False,
            "window_visible": self.window.isVisible() if self.window else False,
            "window_title": self.window.windowTitle() if self.window else "",
            "window_url": self.window._web.url().toString() if self.window else "",
            "theme": self.theme.current,
            "menu_items": [a.text() for a in self.tray_menu.actions()] if self.tray_menu else [],
            "update_action_visible": self.act_update.isVisible(),
            "backend": str(self.backend.status()),
        }
        import json
        print("DSH_DESKTOP_SELF_TEST_BEGIN")
        print(json.dumps(report, ensure_ascii=False, indent=2))
        print("DSH_DESKTOP_SELF_TEST_END")
        sys.stdout.flush()

    # ----- tray -----

    def _build_tray(self) -> None:
        self.tray = QSystemTrayIcon()
        self.tray.setIcon(self._tray_icon())
        self.tray.setToolTip(APP_NAME)
        self.tray.activated.connect(self._on_tray_activated)
        # Left-click also toggles window on KDE Plasma 6 (some panels route this).
        self.tray_menu = QMenu()

        self.act_show = QAction("显示桌面", self.tray_menu)
        self.act_hide = QAction("隐藏桌面", self.tray_menu)
        self.act_check = QAction("检查更新", self.tray_menu)
        self.act_update = QAction("更新到最新版", self.tray_menu)
        self.act_update.setVisible(False)  # only after a check reveals a new version
        self.act_restart = QAction("重启桌面", self.tray_menu)
        self.act_quit = QAction("退出", self.tray_menu)

        self.act_show.triggered.connect(self._show_window)
        self.act_hide.triggered.connect(self._hide_window)
        self.act_check.triggered.connect(self._check_updates)
        self.act_update.triggered.connect(self._perform_update)
        self.act_restart.triggered.connect(self._restart_app)
        self.act_quit.triggered.connect(self._request_quit)

        self.tray_menu.addAction(self.act_show)
        self.tray_menu.addAction(self.act_hide)
        self.tray_menu.addSeparator()
        self.tray_menu.addAction(self.act_check)
        self.tray_menu.addAction(self.act_update)
        self.tray_menu.addSeparator()
        self.tray_menu.addAction(self.act_restart)
        self.tray_menu.addSeparator()
        self.tray_menu.addAction(self.act_quit)
        self.tray.setContextMenu(self.tray_menu)

    def _tray_icon(self) -> QIcon:
        # KDE Plasma 6's StatusNotifierItem happily takes a full QIcon with
        # multiple sizes. We synthesise one from the scheme's pixmaps.
        scheme = self.theme.current
        icon = QIcon()
        for size in (22, 32, 48, 64):
            icon.addPixmap(pixmap_for_scheme(scheme, size=size))
        return icon

    def _on_theme_changed(self, scheme: str) -> None:
        if self.tray is not None:
            self.tray.setIcon(self._tray_icon())
        if self.window is not None:
            self.window.setWindowIcon(app_icon(scheme))
        self.qt_app.setWindowIcon(app_icon(scheme))

    def _on_tray_activated(self, reason) -> None:
        # Toggle window on left-click; KDE Plasma usually routes left-click
        # to ``activated`` with Trigger=1.
        from PyQt6.QtWidgets import QSystemTrayIcon as STI
        if reason in (STI.ActivationReason.Trigger, STI.ActivationReason.DoubleClick):
            self._toggle_window()

    def _toggle_window(self) -> None:
        if self.window is None:
            return
        if self.window.isVisible():
            self._hide_window()
        else:
            self._show_window()

    def _show_window(self) -> None:
        if self.window is None:
            return
        self.window.show()
        self.window.setWindowState(self.window.windowState() & ~Qt.WindowState.WindowMinimized)
        self.window.activateWindow()
        self.window.raise_()

    def _hide_window(self) -> None:
        if self.window is not None:
            self.window.hide()

    def _refresh_tray_menu(self) -> None:
        """Re-evaluate visibility of dynamic items (update-to-latest, restart)."""
        if self.tray_menu is None:
            return
        # Show 'update-to-latest' only after a positive check.
        if getattr(self, "_update_available", False):
            self.act_update.setVisible(True)
        # 'Restart desktop' is always visible - it's the standard recovery path.

    # ----- actions -----

    def _check_updates(self) -> None:
        if self._update_in_progress:
            return
        self.log("tray: checking for updates")
        # Run in a short QTimer to keep the UI responsive.
        QTimer.singleShot(0, self._do_check_updates)

    def _do_check_updates(self) -> None:
        # Use a worker thread to avoid blocking the UI.
        import threading

        result_holder: dict[str, UpdateStatus] = {}

        def worker() -> None:
            result_holder["status"] = check_for_update()

        t = threading.Thread(target=worker, daemon=True)
        t.start()
        # Poll briefly so the dialog is modal but the thread doesn't freeze.
        for _ in range(80):  # ~8 seconds at 100ms
            QApplication.processEvents()
            if not t.is_alive():
                break
            QApplication.processEvents()
            import time as _time
            _time.sleep(0.1)
        status = result_holder.get("status")
        if status is None:
            QMessageBox.warning(None, APP_NAME, "更新检查超时，请稍后再试。")
            return

        if status.update_available:
            self._update_available = True
            self.act_update.setVisible(True)
        else:
            self._update_available = False
            self.act_update.setVisible(False)
        self._refresh_tray_menu()

        dlg = UpdateDialog(self.window, status)
        dlg.exec()

    def _perform_update(self) -> None:
        if self._update_in_progress:
            return
        self._update_in_progress = True
        status = check_for_update()
        if not status.update_available:
            self._update_in_progress = False
            return
        dlg = UpdateDialog(self.window, status)
        dlg.exec()
        self._update_in_progress = False

    def _restart_app(self) -> None:
        """Restart the dsh web backend (systemd unit) and reload the window."""
        self.log("tray: restart requested")
        ok = self.backend.restart()
        if not ok:
            QMessageBox.warning(None, APP_NAME, "无法重启 dsh web 服务，请查看日志。")
            return
        # Reload the window once the backend answers.
        if self.window is not None:
            QTimer.singleShot(800, lambda: self.window.setUrl(QUrl(self.backend.url + "/")))

    def _request_quit(self, *, silent: bool = False) -> None:
        if self._shutdown_started:
            return
        self._shutdown_started = True

        if silent:
            self._perform_quit(stop_backend=False)
            return

        status: BackendStatus = self.backend.status()
        dlg = ExitDialog(
            self.window,
            backend_url=self.backend.url,
            active_tasks=status.active_tasks,
            mode=self.backend.mode,
            parent_window_title=self.window.windowTitle() if self.window else "",
        )
        if dlg.exec() != QDialog.DialogCode.Accepted:
            self._shutdown_started = False
            return
        stop_backend = dlg.bg_checkbox.isChecked()
        self._perform_quit(stop_backend=stop_backend)

    def _perform_quit(self, *, stop_backend: bool) -> None:
        self.log(f"quit: stop_backend={stop_backend}")
        if stop_backend:
            try:
                self.backend.stop()
            except Exception as exc:
                self.log(f"quit: backend.stop failed: {exc}")
        # Tear down Qt.
        try:
            if self.tray is not None:
                self.tray.hide()
        except Exception:
            pass
        try:
            if self.window is not None:
                self.window.close()
                self.window.deleteLater()
        except Exception:
            pass
        self.qt_app.quit()

    # ----- logger -----

    def _make_logger(self, log_file: Optional[str]):
        sink = []
        if log_file:
            try:
                lf = open(log_file, "a", encoding="utf-8")
                lf.write(f"\n--- dsh-desktop started {os.uname().nodename} pid={os.getpid()} ---\n")
                lf.flush()
            except OSError as exc:
                print(f"warning: cannot open {log_file}: {exc}", file=sys.stderr)
                lf = None
        else:
            lf = None

        def log(msg: str) -> None:
            line = f"[dsh-desktop] {msg}"
            sink.append(line)
            # Cap in-memory log.
            if len(sink) > 1000:
                del sink[:500]
            print(line, file=sys.stderr, flush=True)
            if lf is not None:
                try:
                    lf.write(line + "\n")
                    lf.flush()
                except OSError:
                    pass

        return log

    # ----- self-test (used by packaging/smoke.sh) -----

    def _self_test(self) -> int:
        """Boot the full app, dump a structured report, then quit."""
        # Force offscreen mode for repeatable CI runs.
        os.environ["QT_QPA_PLATFORM"] = "offscreen"
        # Run the standard startup path.
        try:
            rc = self.run()
        except Exception as exc:
            import traceback
            print(f"self-test: 启动失败 {exc!r}", file=sys.stderr)
            traceback.print_exc()
            return 4
        return rc


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="dsh-desktop", description=APP_NAME)
    parser.add_argument("--url", default=DEFAULT_URL,
                        help=f"dsh web URL to attach to (default: {DEFAULT_URL})")
    parser.add_argument("--log-file", default=None,
                        help="Optional path to write a persistent log")
    parser.add_argument("--smoke", action="store_true",
                        help="Headless smoke mode: don't open window, just verify backend")
    parser.add_argument("--self-test", action="store_true",
                        help="Boot the full app, dump a structured report, then quit. "
                             "Used by packaging/smoke.sh.")
    return parser.parse_args(argv)


def main(argv: Optional[list[str]] = None) -> int:
    args = _parse_args(list(argv) if argv is not None else sys.argv[1:])
    if args.smoke:
        # Headless path - useful for CI and packaging checks.
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        from .backend import DshBackend
        b = DshBackend(url=args.url)
        s = b.status()
        print(f"smoke: backend running={s.running} mode={s.mode.value} url={s.url} detail={s.detail}")
        return 0 if s.running else 1
    app = DshDesktopApp(args)
    if args.self_test:
        return app._self_test()
    return app.run()


if __name__ == "__main__":
    raise SystemExit(main())
