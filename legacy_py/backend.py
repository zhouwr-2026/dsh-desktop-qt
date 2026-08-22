"""
Backend supervisor for the dsh web service.

Two strategies:
  1. systemd-managed (default): use the existing /etc/systemd/system/dsh-web.service
     (or a per-user dsh-web.service). We treat it as the source of truth and only
     start/stop/check it. This is what we want on Arch + KDE6 because it already
     runs at boot and survives logout of the desktop session.
  2. supervised subprocess: spawn `dsh web` directly and watch its lifecycle. Used
     when systemd is unavailable or the user explicitly opted into a sandboxed
     desktop instance.

The supervisor never owns the official dsh source - it always delegates to the
system-installed `@deepseek-ai/dsh` CLI (or the user-provided DSH_BIN env override)
which in turn loads the official Cordis profile tree.
"""
from __future__ import annotations

import enum
import json
import os
import shutil
import signal
import subprocess
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional


DEFAULT_URL = "http://127.0.0.1:3080"
HEALTH_TIMEOUT_S = 1.5
DSH_HOME = Path(os.environ.get("DSH_HOME", str(Path.home() / ".dsh")))
SYSTEMD_UNIT_NAME = "dsh-web.service"


class BackendMode(str, enum.Enum):
    SYSTEMD = "systemd"
    SUPERVISED = "supervised"


def _run(cmd: list[str], timeout: float = 5.0) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, check=False)


def _detect_dsh_bin() -> str:
    """Locate the dsh CLI executable; honour DSH_BIN override, fall back to PATH."""
    override = os.environ.get("DSH_BIN")
    if override and Path(override).exists():
        return override
    found = shutil.which("dsh")
    if found:
        return found
    # Last-ditch: npm global install location.
    candidates = [
        "/usr/bin/dsh",
        "/usr/local/bin/dsh",
        str(Path.home() / ".local/bin/dsh"),
    ]
    for c in candidates:
        if Path(c).exists():
            return c
    raise FileNotFoundError("dsh binary not found; install @deepseek-ai/dsh or set DSH_BIN")


def _detect_systemd_unit() -> Optional[str]:
    """Return the systemd unit name if a dsh-web service is configured (system or user)."""
    # System unit (managed by root). Prefer this if present.
    for prefix in ("/etc/systemd/system", "/usr/lib/systemd/system"):
        if Path(prefix, SYSTEMD_UNIT_NAME).exists():
            return SYSTEMD_UNIT_NAME
    # User unit (~/.config/systemd/user/dsh-web.service).
    user_unit = Path.home() / ".config/systemd/user" / SYSTEMD_UNIT_NAME
    if user_unit.exists():
        return SYSTEMD_UNIT_NAME
    return None


@dataclass
class BackendStatus:
    """Snapshot of the dsh web backend."""
    running: bool
    url: str
    mode: BackendMode
    detail: str = ""
    active_tasks: int = 0


class DshBackend:
    """Owns the lifecycle of one dsh web backend instance."""

    def __init__(
        self,
        url: str = DEFAULT_URL,
        mode: Optional[BackendMode] = None,
        log_hook: Optional[Callable[[str], None]] = None,
        log: Optional[Callable[[str], None]] = None,
    ) -> None:
        self.url = url.rstrip("/")
        # Accept either the documented ``log_hook`` kwarg or the shorter
        # ``log`` alias used by ``DshDesktopApp``.
        self._log: Callable[[str], None] = log_hook or log or (lambda m: None)
        if mode is None:
            mode = BackendMode.SYSTEMD if _detect_systemd_unit() else BackendMode.SUPERVISED
        self.mode = mode
        self._dsh_bin = _detect_dsh_bin()
        self._proc: Optional[subprocess.Popen[str]] = None
        self._supervised_started_at: float = 0.0

    # ----- public API -----

    def is_running(self) -> bool:
        return self._probe()

    def status(self) -> BackendStatus:
        running = self._probe()
        detail = self._detail()
        active = self._count_active_tasks() if running else 0
        return BackendStatus(
            running=running,
            url=self.url,
            mode=self.mode,
            detail=detail,
            active_tasks=active,
        )

    def start(self) -> bool:
        if self._probe():
            self._log(f"backend: already running at {self.url}")
            return True
        if self.mode == BackendMode.SYSTEMD:
            return self._systemctl("start")
        return self._spawn_supervised()

    def stop(self, *, force: bool = False) -> bool:
        if self.mode == BackendMode.SYSTEMD:
            return self._systemctl("stop", force=force)
        return self._stop_supervised(force=force)

    def restart(self) -> bool:
        if self.mode == BackendMode.SYSTEMD:
            return self._systemctl("restart")
        was_running = self._stop_supervised(force=True)
        ok = self._spawn_supervised()
        return ok or was_running  # report success if either side succeeded

    # ----- internals -----

    def _probe(self) -> bool:
        try:
            req = urllib.request.Request(self.url + "/", method="GET")
            with urllib.request.urlopen(req, timeout=HEALTH_TIMEOUT_S) as resp:
                return 200 <= resp.status < 500  # 200 = html; 404 on /api means server alive
        except (urllib.error.URLError, ConnectionError, TimeoutError, OSError):
            return False

    def _detail(self) -> str:
        if self.mode == BackendMode.SYSTEMD:
            unit = _detect_systemd_unit()
            if not unit:
                return "systemd mode: no dsh-web.service installed"
            try:
                res = _run(["systemctl", "--no-pager", "-q", "is-active", unit], timeout=3)
                state = res.stdout.strip() or res.stderr.strip() or "unknown"
                return f"systemd unit {unit}: {state}"
            except (FileNotFoundError, subprocess.TimeoutExpired):
                return f"systemd unit {unit}: systemctl unavailable"
        if self._proc is not None:
            rc = self._proc.poll()
            return f"supervised pid={self._proc.pid} exit={rc}"
        return "supervised: not started"

    def _systemctl(self, verb: str, *, force: bool = False) -> bool:
        unit = _detect_systemd_unit()
        if not unit:
            self._log(f"backend: no systemd unit found; cannot {verb}")
            return False
        # --user is required for user units; harmless for system units when polkit allows.
        cmd = ["systemctl", verb, unit]
        if unit.startswith("dsh-") and Path("/etc/systemd/system", unit).exists():
            # system unit - escalate to root only for start/stop, never read-only.
            if verb in ("start", "stop", "restart") and os.geteuid() != 0:
                cmd = ["pkexec", "--disable-internal-agent"] + cmd
        self._log(f"backend: {' '.join(cmd)}")
        try:
            res = _run(cmd, timeout=20)
        except subprocess.TimeoutExpired:
            self._log(f"backend: systemctl {verb} timed out")
            return False
        ok = res.returncode == 0
        self._log(f"backend: systemctl {verb} -> rc={res.returncode}")
        return ok

    def _spawn_supervised(self) -> bool:
        if self._proc is not None and self._proc.poll() is None:
            return True
        env = os.environ.copy()
        env.setdefault("DSH_HOME", str(DSH_HOME))
        # Bind to 3080 by default so it matches the systemd unit; users can override
        # via DSH_DESKTOP_PORT.
        port = os.environ.get("DSH_DESKTOP_PORT", "3080")
        cmd = [self._dsh_bin, "web", "--host", "127.0.0.1", "--port", port]
        self._log(f"backend: spawning {' '.join(cmd)}")
        try:
            self._proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env=env,
                start_new_session=True,  # own pgid so we can SIGTERM the whole group
                text=True,
            )
        except OSError as exc:
            self._log(f"backend: spawn failed: {exc}")
            return False
        self._supervised_started_at = time.monotonic()
        # Wait for HTTP readiness (capped ~15s).
        deadline = self._supervised_started_at + 15.0
        while time.monotonic() < deadline:
            if self._proc.poll() is not None:
                self._log("backend: supervised process exited early")
                return False
            if self._probe():
                return True
            time.sleep(0.25)
        self._log("backend: supervised process did not become ready in 15s")
        return False

    def _stop_supervised(self, *, force: bool = False) -> bool:
        if self._proc is None or self._proc.poll() is not None:
            return True
        try:
            pgid = os.getpgid(self._proc.pid)
            sig = signal.SIGKILL if force else signal.SIGTERM
            os.killpg(pgid, sig)
        except ProcessLookupError:
            self._proc = None
            return True
        try:
            self._proc.wait(timeout=8 if not force else 3)
        except subprocess.TimeoutExpired:
            if not force:
                self._log("backend: graceful stop timed out; escalating to SIGKILL")
                return self._stop_supervised(force=True)
        self._proc = None
        return True

    def _count_active_tasks(self) -> int:
        """Best-effort count of running tasks/jobs via the dsh web telemetry endpoint."""
        # The Host exposes /api/sessions and /api/jobs; we don't know the exact schema
        # of every profile, so we try a few cheap probes and treat any failure as 0.
        candidates = ["/api/jobs", "/api/sessions?limit=1", "/api/runs?limit=1"]
        for path in candidates:
            try:
                req = urllib.request.Request(self.url + path, method="GET")
                with urllib.request.urlopen(req, timeout=1.0) as resp:
                    if resp.status != 200:
                        continue
                    body = resp.read(64 * 1024).decode("utf-8", "replace")
                    return self._tasks_in_payload(body)
            except (urllib.error.URLError, ConnectionError, TimeoutError, OSError, ValueError):
                continue
        return 0

    @staticmethod
    def _tasks_in_payload(body: str) -> int:
        try:
            data = json.loads(body)
        except json.JSONDecodeError:
            return 0
        if isinstance(data, list):
            running = 0
            for item in data:
                if not isinstance(item, dict):
                    continue
                state = (item.get("state") or item.get("status") or "").lower()
                if state in {"running", "active", "in_progress", "pending"}:
                    running += 1
            return running
        if isinstance(data, dict):
            for key in ("active", "running", "in_flight", "pending"):
                v = data.get(key)
                if isinstance(v, int):
                    return v
                if isinstance(v, list):
                    return len(v)
        return 0


def probe_default_url(url: str = DEFAULT_URL) -> bool:
    """Module-level helper: just probe the URL."""
    return DshBackend(url=url).is_running()
