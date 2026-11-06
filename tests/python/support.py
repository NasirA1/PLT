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


def environment() -> dict[str, str]:
    env = dict(os.environ)
    env.setdefault("PYTHONIOENCODING", "utf-8")
    return env
