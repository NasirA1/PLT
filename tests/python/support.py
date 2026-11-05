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
def display_available() -> bool:
    try:
        import tkinter

        root = tkinter.Tk()
        root.destroy()
        del root
        import gc

        gc.collect()  # free the Tk interpreter on this (the main) thread
        return True
    except Exception:  # tkinter.TclError when there is no display
        return False


def requires_display(test):
    return unittest.skipUnless(display_available(), "no display available for Tk")(test)


def tool(name: str) -> str:
    directory = bin_dir()
    assert directory is not None
    return str(directory / f"{name}{EXE_SUFFIX}")


def environment() -> dict[str, str]:
    env = dict(os.environ)
    env.setdefault("PYTHONIOENCODING", "utf-8")
    return env
