"""Shared helpers for the Python tests."""

from __future__ import annotations

import functools
import os
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "ide") not in sys.path:
    sys.path.insert(0, str(ROOT / "ide"))

from thunder_ide.core.toolchain import EXE_SUFFIX, ToolchainError, find_bin_dir  # noqa: E402

SAMPLES = ROOT / "samples" / "en"
PASHTO_SAMPLES = ROOT / "samples" / "ps"


@functools.lru_cache(maxsize=None)
def bin_dir() -> Path | None:
    try:
        return find_bin_dir()
    except ToolchainError:
        return None


def requires_toolchain(test):
    """Skips a test (or test class) when thunderc/thundervm have not been built."""
    return unittest.skipIf(bin_dir() is None, "thunderc/thundervm not built (set THUNDER_BIN)")(test)


@functools.lru_cache(maxsize=None)
def qt_available() -> bool:
    """True if PySide6 can be imported.

    The tests drive Qt's offscreen platform, so no windows flash up while they run (and no display
    is needed). Set THUNDER_TEST_ON_SCREEN=1 to watch them on the real display instead.
    """
    if os.environ.get("THUNDER_TEST_ON_SCREEN") != "1":
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        # Qt's offscreen platform does not use the system's font engine: it reads font files
        # itself, from QT_QPA_FONTDIR or PySide6/lib/fonts. Qt no longer ships fonts, so on
        # Windows (where fontconfig is not there to help) it would find none and fall back to
        # a font with made-up metrics, and every test that measures text would fail. Point it
        # at the system's fonts instead.
        if sys.platform == "win32":
            fonts = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts"
            if fonts.is_dir():
                os.environ.setdefault("QT_QPA_FONTDIR", str(fonts))
    try:
        import PySide6.QtWidgets  # noqa: F401
    except ImportError:
        return False
    return True


def requires_qt(test):
    return unittest.skipUnless(qt_available(), "PySide6 (Qt for Python) is not installed")(test)


def tool(name: str) -> str:
    directory = bin_dir()
    assert directory is not None
    return str(directory / f"{name}{EXE_SUFFIX}")


def library_file_name(name: str) -> str:
    """The file a native module called ``name`` has on this platform (see os::library_file_name)."""
    if sys.platform == "win32":
        return f"{name}.dll"
    return f"lib{name}.dylib" if sys.platform == "darwin" else f"lib{name}.so"


@functools.lru_cache(maxsize=None)
def version() -> str:
    """The toolchain's version, as thunderc --version reports it."""
    import subprocess
    output = subprocess.run([tool("thunderc"), "--version"], capture_output=True, text=True).stdout
    return output.split()[-1].strip() if output else ""


def environment() -> dict[str, str]:
    env = dict(os.environ)
    env.setdefault("PYTHONIOENCODING", "utf-8")
    return env
