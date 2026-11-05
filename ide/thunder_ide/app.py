"""Starts the Thunder IDE: ``python -m thunder_ide [--bin DIR] [--ui tk] [file.th]``."""

from __future__ import annotations

import argparse
from typing import Optional

from thunder_ide.core.controller import IdeController
from thunder_ide.core.settings import Settings, default_settings_path
from thunder_ide.core.toolchain import Toolchain, ToolchainError

BACKENDS = ("tk",)  # a Qt backend would be added here (see thunder_ide.core.ui)


def create_backend(name: str):
    if name == "tk":
        from thunder_ide.tk.backend import TkBackend

        return TkBackend()
    raise ValueError(f"unknown user interface toolkit {name!r}")


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(prog="thunder_ide", description="The Thunder educational IDE.")
    parser.add_argument("file", nargs="?", help="a Thunder source file to open")
    parser.add_argument("--bin", help="directory containing thunderc and thundervm")
    parser.add_argument("--ui", choices=BACKENDS, default="tk", help="user interface toolkit (default: tk)")
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
