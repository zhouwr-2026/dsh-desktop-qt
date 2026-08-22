"""
Session-logs download interceptor.

The official DSH Web app triggers downloads by injecting ``<a download="...">``
elements and clicking them. Inside ``QWebEngineView`` that surfaces as the
``QWebEngineProfile.downloadRequested`` signal.

We intercept it and instead of letting Chromium save the file silently, we:
  1. Show a native ``QFileDialog.getSaveFileName`` with the suggested filename
     (``dsh-session-<id>.zip``).
  2. Stream the URL content to the chosen path with a progress dialog.
  3. Emit a desktop notification on completion (or failure).

This is the requirement #5 path.
"""
from __future__ import annotations

import os
import shutil
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Callable, Optional

from PyQt6.QtCore import QObject, QUrl, pyqtSignal
from PyQt6.QtWidgets import (
    QFileDialog,
    QMessageBox,
    QProgressDialog,
)


# URL prefixes that look like session exports. The Host endpoint is
# ``/api/session.export?sessionId=...`` per the upstream
# ``packages/session-query/session-log-export`` controller.
SESSION_EXPORT_PATHS = ("/api/session.export",)


def _looks_like_session_export(url: QUrl) -> bool:
    path = url.path() or url.toString()
    return any(p in path for p in SESSION_EXPORT_PATHS)


def _default_filename(url: QUrl) -> str:
    # The official controller emits ``dsh-session-<id>.zip``; fall back to last
    # path component or a generic name.
    candidate = url.query().split("sessionId=", 1)[-1].split("&", 1)[0]
    if candidate:
        safe = "".join(c if c.isalnum() or c in "-_" else "_" for c in candidate)
        return f"dsh-session-{safe}.zip"
    name = Path(url.path() or "").name
    return name or "dsh-session.zip"


class DownloadWorker(QObject):
    """Stream one URL to disk; emits ``progress(int)`` 0..100 and ``done(bool, str)``."""

    progress = pyqtSignal(int)
    done = pyqtSignal(bool, str)

    def __init__(self, url: str, dest: str, parent: Optional[QObject] = None) -> None:
        super().__init__(parent)
        self._url = url
        self._dest = dest
        self._cancelled = False

    def cancel(self) -> None:
        self._cancelled = True

    def run(self) -> None:
        try:
            req = urllib.request.Request(self._url, method="GET")
            with urllib.request.urlopen(req, timeout=60) as resp:
                total = int(resp.headers.get("Content-Length") or 0)
                written = 0
                Path(self._dest).parent.mkdir(parents=True, exist_ok=True)
                with open(self._dest, "wb") as fh:
                    while True:
                        if self._cancelled:
                            fh.close()
                            try:
                                os.unlink(self._dest)
                            except OSError:
                                pass
                            self.done.emit(False, "cancelled")
                            return
                        chunk = resp.read(64 * 1024)
                        if not chunk:
                            break
                        fh.write(chunk)
                        written += len(chunk)
                        if total > 0:
                            pct = min(100, int(written * 100 / total))
                            self.progress.emit(pct)
                        else:
                            # Indeterminate: cycle 0..95% based on bytes received.
                            approx = min(95, written // (32 * 1024))
                            self.progress.emit(approx)
            self.progress.emit(100)
            self.done.emit(True, self._dest)
        except (urllib.error.URLError, ConnectionError, TimeoutError, OSError) as exc:
            self.done.emit(False, f"{type(exc).__name__}: {exc}")


class DownloadInterceptor(QObject):
    """Owns the policy that turns intercepted downloads into native save dialogs."""

    def __init__(self, parent: Optional[QObject] = None,
                 *, url_predicate: Optional[Callable[[QUrl], bool]] = None,
                 log: Optional[Callable[[str], None]] = None) -> None:
        super().__init__(parent)
        self._predicate = url_predicate or _looks_like_session_export
        self._log = log or (lambda m: None)
        self._in_flight: list[tuple[DownloadWorker, QProgressDialog]] = []

    def handle(self, url: QUrl, suggested_filename: str) -> None:
        """Called from the ``downloadRequested`` slot."""
        if not self._predicate(url):
            # Not a session log; let Chromium handle it (will land in ~/Downloads).
            self._log(f"downloads: passthrough {url.toString()}")
            return
        self._log(f"downloads: intercepting {url.toString()}")
        suggested = suggested_filename or _default_filename(url)
        start_path = str(Path.home() / "Downloads" / suggested)
        path, _filter = QFileDialog.getSaveFileName(
            None,
            "保存会话日志",
            start_path,
            "ZIP 压缩包 (*.zip);;所有文件 (*)",
        )
        if not path:
            self._log("downloads: user cancelled save dialog")
            return

        # Progress dialog (modal, cancellable).
        progress = QProgressDialog(
            f"正在下载 {Path(path).name}", "取消", 0, 100, None,
        )
        progress.setWindowTitle("DSH Desktop")
        progress.setWindowModality(progress.windowModality().__class__.ApplicationModal
                                   if hasattr(progress.windowModality(), '__class__') else
                                   progress.windowModality())
        progress.setMinimumDuration(0)
        progress.setValue(0)
        progress.show()

        worker = DownloadWorker(url.toString(), path)
        entry = (worker, progress)
        self._in_flight.append(entry)

        def _on_progress(pct: int) -> None:
            progress.setValue(pct)

        def _on_done(ok: bool, detail: str) -> None:
            progress.close()
            try:
                self._in_flight.remove(entry)
            except ValueError:
                pass
            if ok:
                QMessageBox.information(
                    None, "下载完成",
                    f"会话日志已保存到：\n{detail}",
                )
            else:
                QMessageBox.warning(
                    None, "下载失败",
                    f"无法下载会话日志：\n{detail}",
                )

        progress.canceled.connect(worker.cancel)
        worker.progress.connect(_on_progress)
        worker.done.connect(_on_done)

        thread = threading.Thread(target=worker.run, name=f"dsh-download-{int(time.time()*1000)}",
                                  daemon=True)
        thread.start()
