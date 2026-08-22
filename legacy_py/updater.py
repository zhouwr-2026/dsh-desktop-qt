"""
Update checker for `@deepseek-ai/dsh`.

We never reach outside the official npm registry: the desktop wrapper owns no
codebase of its own, it only needs the latest CLI to keep DSH itself current.
If a newer version is published we delegate installation to ``pkexec`` so the
caller gets a clean polkit prompt (and we never silently escalate).

If the host is offline or the registry is unreachable, ``check_for_update``
returns ``None`` and the caller treats that as "indeterminate".
"""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


NPM_REGISTRY = "https://registry.npmjs.org/@deepseek-ai/dsh"
LOCAL_BIN = "/usr/bin/dsh"
LOCAL_GLOBAL_NODE_MODULES = "/usr/lib/node_modules/@deepseek-ai/dsh/package.json"


@dataclass
class UpdateStatus:
    current: Optional[str]
    latest: Optional[str]
    update_available: bool
    detail: str = ""


# strict semver match for stable; we treat -rc.X etc as newer than missing.
_SEMVER_RE = re.compile(
    r"^[vV]?(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)(?:-(?P<pre>[0-9A-Za-z.-]+))?(?:\+(?P<build>[0-9A-Za-z.-]+))?$"
)


def parse_semver(s: str) -> Optional[tuple[int, int, int, str, str]]:
    if not s:
        return None
    m = _SEMVER_RE.match(s.strip())
    if not m:
        return None
    return (
        int(m.group("major")),
        int(m.group("minor")),
        int(m.group("patch")),
        m.group("pre") or "",
        m.group("build") or "",
    )


def _cmp(a: tuple[int, int, int, str, str], b: tuple[int, int, int, str, str]) -> int:
    if a[:3] != b[:3]:
        return (a[:3] > b[:3]) - (a[:3] < b[:3])
    # SemVer 2.0: no prerelease > prerelease.
    if not a[3] and b[3]:
        return 1
    if a[3] and not b[3]:
        return -1
    if a[3] != b[3]:
        # Lexical compare on identifiers split by '.'.
        ai = a[3].split(".")
        bi = b[3].split(".")
        for x, y in zip(ai, bi):
            if x == y:
                continue
            xi = _as_int(x)
            yi = _as_int(y)
            if xi is not None and yi is not None:
                return (xi > yi) - (xi < yi)
            if xi is not None:
                return -1  # numeric < non-numeric
            if yi is not None:
                return 1
            return (x > y) - (x < y)
        return (len(ai) > len(bi)) - (len(ai) < len(bi))
    return 0


def _as_int(s: str) -> Optional[int]:
    try:
        return int(s)
    except ValueError:
        return None


def read_local_version() -> Optional[str]:
    """Read the version installed locally on the system."""
    # Prefer the package.json that ships with the global install.
    candidates = [
        Path(LOCAL_GLOBAL_NODE_MODULES),
        Path("/usr/local/lib/node_modules/@deepseek-ai/dsh/package.json"),
        Path.home() / ".local/lib/node_modules/@deepseek-ai/dsh/package.json",
    ]
    for c in candidates:
        if c.exists():
            try:
                data = json.loads(c.read_text(encoding="utf-8"))
                v = data.get("version")
                if isinstance(v, str):
                    return v
            except (OSError, json.JSONDecodeError):
                pass
    # Fallback to ``dsh --version``.
    bin_path = shutil.which("dsh") or LOCAL_BIN
    if Path(bin_path).exists():
        try:
            res = subprocess.run(
                [bin_path, "--version"], capture_output=True, text=True, timeout=4, check=False,
            )
            v = (res.stdout or res.stderr).strip()
            if v:
                return v
        except (OSError, subprocess.TimeoutExpired):
            pass
    return None


def fetch_latest_version(timeout_s: float = 8.0) -> Optional[str]:
    """Fetch the latest stable version of @deepseek-ai/dsh from the npm registry."""
    req = urllib.request.Request(
        f"{NPM_REGISTRY}/latest",
        method="GET",
        headers={"Accept": "application/json", "User-Agent": "dsh-desktop/1.0"},
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout_s) as resp:
            if resp.status != 200:
                return None
            data = json.loads(resp.read().decode("utf-8"))
            v = data.get("version")
            return v if isinstance(v, str) else None
    except (urllib.error.URLError, ConnectionError, TimeoutError, OSError, json.JSONDecodeError):
        return None


def check_for_update(timeout_s: float = 8.0) -> UpdateStatus:
    current = read_local_version()
    latest = fetch_latest_version(timeout_s=timeout_s)
    if not current or not latest:
        return UpdateStatus(
            current=current,
            latest=latest,
            update_available=False,
            detail="offline or unable to query npm registry",
        )
    cv = parse_semver(current)
    lv = parse_semver(latest)
    if cv is None or lv is None:
        # Fall back to string compare.
        return UpdateStatus(
            current=current,
            latest=latest,
            update_available=latest != current,
            detail="semver parse failed; using string compare",
        )
    return UpdateStatus(
        current=current,
        latest=latest,
        update_available=_cmp(lv, cv) > 0,
        detail="ok",
    )


def perform_update(log: Optional[object] = None) -> tuple[bool, str]:
    """Run ``pkexec npm install -g @deepseek-ai/dsh@latest`` and return (ok, output).

    ``log`` is an optional callable that receives each line as it arrives.
    """
    if shutil.which("pkexec") is None:
        return False, "pkexec is not installed (install polkit)."
    if shutil.which("npm") is None:
        return False, "npm is not installed."
    cmd = ["pkexec", "--disable-internal-agent", "npm", "install", "-g",
           "@deepseek-ai/dsh@latest", "--no-audit", "--no-fund"]
    if log:
        log(f"updater: running {' '.join(cmd)}")
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    except OSError as exc:
        return False, f"failed to launch updater: {exc}"
    chunks: list[str] = []
    assert proc.stdout is not None
    for line in proc.stdout:
        chunks.append(line)
        if log:
            try:
                log(line.rstrip())
            except Exception:
                pass
    rc = proc.wait(timeout=180)
    output = "".join(chunks).strip()
    return rc == 0, output or f"exit code {rc}"


# Quick self-test
if __name__ == "__main__":
    print("current:", read_local_version())
    s = check_for_update()
    print("status :", s)
