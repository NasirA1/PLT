"""Starts the Thunder IDE: ``python -m thunder_ide [--bin DIR] [--ui qt] [file.th]``."""

from __future__ import annotations

import argparse
from typing import Optional

from thunder_ide.core.controller import IdeController
from thunder_ide.core.settings import Settings, default_settings_path
from thunder_ide.core.toolchain import Toolchain, ToolchainError

BACKENDS = ("qt",)  # another toolkit would be added here (see thunder_ide.core.ui)

PYSIDE_HINT = ("The Thunder IDE needs PySide6 (Qt for Python). Install it with\n"
               "    python -m pip install PySide6\n"
               "(on Linux, if pip refuses, create a virtual environment first: python3 -m venv .venv && "
               ". .venv/bin/activate).")


def create_backend(name: str):
    if name == "qt":
        try:
            from thunder_ide.qt.backend import QtBackend
        except ImportError as exc:  # pragma: no cover - depends on the machine
            raise SystemExit(f"error: {exc}\n{PYSIDE_HINT}") from exc
        return QtBackend()
    raise ValueError(f"unknown user interface toolkit {name!r}")


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(prog="thunder_ide", description="The Thunder educational IDE.")
    parser.add_argument("file", nargs="?", help="a Thunder source file to open")
    parser.add_argument("--bin", help="directory containing thunderc and thundervm")
    parser.add_argument("--ui", choices=BACKENDS, default="qt", help="user interface toolkit (default: qt)")
    args = parser.parse_args(argv)

    backend = create_backend(args.ui)
    try:
        toolchain: Optional[Toolchain] = Toolchain.locate(args.bin)
    except ToolchainError as exc:
        toolchain = None
        error = str(exc)
    else:
        error = ""
    settings_path = default_settings_path()
    controller = IdeController(backend, toolchain, Settings.load(settings_path), settings_path)
    if error:
        controller.window.show_error("Thunder", error)
    if args.file:
        controller.open_file(args.file)
    backend.run()
    return 0
