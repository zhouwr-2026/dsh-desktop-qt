"""Unit tests for the DSH Desktop modules.

Run with::

    QT_QPA_PLATFORM=offscreen python3 -m unittest tests.test_units
"""
import os
import sys
import unittest
from pathlib import Path

# Ensure we exercise the local package, not any installed one.
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

# QtWebEngine needs a few extra switches when there is no display server / GPU.
os.environ.setdefault("QTWEBENGINE_DISABLE_SANDBOX", "1")
os.environ.setdefault("QT_OPENGL", "software")
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

# Importing the app module sets AA_ShareOpenGLContexts BEFORE QApplication is
# created, which is mandatory for QWebEngineProfile to work in PyQt6.
import dsh_desktop.app  # noqa: F401  (side-effect import)


def _ensure_qapp():
    from PyQt6.QtWidgets import QApplication
    return QApplication.instance() or QApplication([])


class TestSemver(unittest.TestCase):
    def test_parse(self):
        from dsh_desktop.updater import parse_semver
        self.assertEqual(parse_semver("1.2.3"), (1, 2, 3, "", ""))
        self.assertEqual(parse_semver("v0.1.0-rc.7"), (0, 1, 0, "rc.7", ""))
        self.assertIsNone(parse_semver("not-a-version"))
        self.assertIsNone(parse_semver("1.2"))  # incomplete

    def test_compare(self):
        from dsh_desktop.updater import _cmp, parse_semver
        s = parse_semver
        self.assertEqual(_cmp(s("0.1.0-rc.8"), s("0.1.0-rc.7")), 1)
        self.assertEqual(_cmp(s("0.1.0-rc.7"), s("0.1.0-rc.8")), -1)
        self.assertEqual(_cmp(s("0.1.0"), s("0.1.0-rc.9")), 1)
        self.assertEqual(_cmp(s("1.0.0-alpha.1"), s("1.0.0-alpha.beta")), -1)
        self.assertEqual(_cmp(s("1.2.4"), s("1.2.3")), 1)


class TestIcons(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._qa = _ensure_qapp()

    def test_light_dark_pixmaps(self):
        from dsh_desktop.icons import pixmap_for_scheme
        for scheme in ("light", "dark"):
            for size in (32, 64, 128):
                pix = pixmap_for_scheme(scheme, size)
                self.assertFalse(pix.isNull(), f"{scheme} {size} is null")
                self.assertEqual(pix.width(), size)

    def test_icons_distinct(self):
        from dsh_desktop.icons import pixmap_for_scheme
        black = pixmap_for_scheme("light", 64)
        white = pixmap_for_scheme("dark", 64)
        self.assertFalse(black.toImage() == white.toImage())


class TestDownloadPredicate(unittest.TestCase):
    def test_session_export_detected(self):
        from PyQt6.QtCore import QUrl
        from dsh_desktop.downloads import _looks_like_session_export
        for u in (
            "http://127.0.0.1:3080/api/session.export?sessionId=abc",
            "http://127.0.0.1:3080/api/session.export?sessionId=abc&includeDescendants=true",
        ):
            self.assertTrue(_looks_like_session_export(QUrl(u)))
        self.assertFalse(_looks_like_session_export(QUrl("http://example.com/file.zip")))


class TestBackend(unittest.TestCase):
    def test_smoke_probe(self):
        from dsh_desktop.backend import DshBackend
        b = DshBackend()
        running = b.is_running()
        self.assertIsInstance(running, bool)


class TestTheme(unittest.TestCase):
    def test_watcher_returns_value(self):
        _ensure_qapp()
        from dsh_desktop.theme import ThemeWatcher
        tw = ThemeWatcher()
        self.assertIn(tw.current, ("light", "dark"))


class TestLoopbackPage(unittest.TestCase):
    """Validate LoopbackWebPage's URL classification without touching the engine.

    Creating a real QWebEngineProfile aborts in headless CI environments
    because Chromium fails to spawn its zygote. The host/URL decision logic
    is what we actually need to verify, so we exercise it via a tiny
    stand-in subclass.
    """

    def test_internal_allowed(self):
        from dsh_desktop.app import LoopbackWebPage
        from PyQt6.QtCore import QUrl

        # Build a subclass that skips the QWebEnginePage ctor entirely.
        captured = []

        class _Stub(LoopbackWebPage):
            def __init__(self):
                self._external_opener = lambda u: captured.append(("external", u.toString()))
                self._log = lambda m: None
                self._is_internal_captured = []

            def acceptNavigationRequest(self, url, nav_type, is_main_frame):  # type: ignore[override]
                # We can't test the parent's acceptNavigationRequest without
                # instantiating QWebEnginePage, but we can call _is_internal
                # which is the pure predicate.
                return self._is_internal(url)

            def _is_internal(self, url):  # type: ignore[override]
                captured.append(("internal?", url.toString(), super()._is_internal(url)))
                return super()._is_internal(url)

        stub = _Stub()
        for url in (
            "http://127.0.0.1:3080/",
            "http://localhost:3080/api/foo",
            "data:text/plain,hello",
        ):
            self.assertTrue(
                stub.acceptNavigationRequest(QUrl(url), None, True),
                f"internal URL should be accepted: {url}",
            )

    def test_external_rejected(self):
        from dsh_desktop.app import LoopbackWebPage
        from PyQt6.QtCore import QUrl

        class _Stub(LoopbackWebPage):
            def __init__(self, opener):
                self._external_opener = opener
                self._log = lambda m: None

            def acceptNavigationRequest(self, url, nav_type, is_main_frame):  # type: ignore[override]
                if self._is_internal(url):
                    return True
                if url.scheme().lower() in {"http", "https"}:
                    try:
                        self._external_opener(url)
                    except Exception:
                        pass
                    return False
                return False

        opener_calls = []

        def opener(u):
            opener_calls.append(u.toString())

        stub = _Stub(opener)
        accepted = stub.acceptNavigationRequest(
            QUrl("https://example.com/article"), None, True,
        )
        self.assertFalse(accepted, "external https link should be rejected")
        self.assertEqual(len(opener_calls), 1)
        self.assertEqual(opener_calls[0], "https://example.com/article")


if __name__ == "__main__":
    unittest.main(verbosity=2)
