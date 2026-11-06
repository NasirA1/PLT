#!/usr/bin/env python3
"""Builds the Thunder toolchain if needed, then starts the IDE (used by run.sh and run.ps1)."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pyenv  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "release"
IS_WINDOWS = os.name == "nt"


def fail(message: str) -> int:
    print(f"error: {message}", file=sys.stderr)
    return 1


def check_python() -> str | None:
    if sys.version_info < (3, 10):
        return f"Python 3.10 or newer is required (this is {sys.version.split()[0]})."
    try:
        pyenv.ensure_python()  # re-executes this script with an interpreter that has PySide6 if needed
    except (RuntimeError, subprocess.CalledProcessError, OSError) as exc:
        return (f"could not set up the Python environment for the IDE: {exc}\n"
                f"Install PySide6 by hand ({sys.executable} -m pip install PySide6) or set THUNDER_PYTHON to an "
                "interpreter that has it.")
    return None


def build() -> int:
    if shutil.which("cmake") is None:
        return fail("CMake was not found on PATH (https://cmake.org/download/).")
    if not (BUILD / "CMakeCache.txt").is_file():
        command = ["cmake", "-S", str(ROOT), "-B", str(BUILD), "-DCMAKE_BUILD_TYPE=Release", "-DTHUNDER_BUILD_TESTS=OFF"]
        if not IS_WINDOWS and shutil.which("ninja"):
            command += ["-G", "Ninja"]
        print("$ " + " ".join(command), flush=True)
        if subprocess.run(command).returncode != 0:
            return fail("CMake configuration failed.")
    command = ["cmake", "--build", str(BUILD), "--config", "Release", "--parallel"]
    print("$ " + " ".join(command), flush=True)
    if subprocess.run(command).returncode != 0:
        return fail("the build failed.")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build the Thunder toolchain and start the IDE.")
    parser.add_argument("--no-build", action="store_true", help="start the IDE without building first")
    parser.add_argument("file", nargs="?", help="a Thunder source file to open")
    args = parser.parse_args(argv)

    problem = check_python()
    if problem:
        return fail(problem)
    if not args.no_build and build() != 0:
        return 1

    env = dict(os.environ)
    env["PYTHONPATH"] = str(ROOT / "ide") + os.pathsep + env.get("PYTHONPATH", "")
    command = [sys.executable, "-m", "thunder_ide", "--bin", str(BUILD / "bin")]
    if args.file:
        command.append(str(Path(args.file).resolve()))
    print("$ " + " ".join(command), flush=True)
    return subprocess.run(command, env=env, cwd=ROOT).returncode


if __name__ == "__main__":
    sys.exit(main())
