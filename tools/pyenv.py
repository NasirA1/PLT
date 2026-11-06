#!/usr/bin/env python3
"""Finds, or creates, a Python interpreter that can run the Thunder IDE (it needs PySide6).

Used by launch.py and run_tests.py so that ``./run.sh`` / ``.\\run.ps1`` work on a fresh machine
without any manual Python set-up:

1. If the running interpreter can import PySide6, it is used as it is.
2. Otherwise a virtual environment is created in ``<repository>/.venv`` (with pip; if the
   system Python lacks ``ensurepip``, pip is bootstrapped from https://bootstrap.pypa.io) and
   PySide6 is installed into it with pip. This happens once; later runs find the environment.
3. The caller re-executes itself with that interpreter.

Set THUNDER_PYTHON to an interpreter to use it instead, or THUNDER_NO_VENV=1 to disable the
automatic environment (the caller then reports what is missing).
"""

from __future__ import annotations

import os
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VENV = ROOT / ".venv"
IS_WINDOWS = os.name == "nt"
GET_PIP_URL = "https://bootstrap.pypa.io/get-pip.py"
REQUIREMENTS = ["PySide6>=6.6"]


def venv_python(venv: Path = VENV) -> Path:
    return venv / ("Scripts/python.exe" if IS_WINDOWS else "bin/python")


def can_import(python: Path | str, module: str) -> bool:
    try:
        return subprocess.run([str(python), "-c", f"import {module}"], capture_output=True, text=True).returncode == 0
    except OSError:
        return False


def say(message: str) -> None:
    print(f"[python environment] {message}", flush=True)


def create_venv(venv: Path = VENV) -> Path:
    """Creates the virtual environment and installs PySide6 into it. Returns its interpreter."""
    python = venv_python(venv)
    if not python.exists():
        say(f"creating a virtual environment in {venv} (first run only)")
        try:
            subprocess.run([sys.executable, "-m", "venv", str(venv)], check=True)
        except subprocess.CalledProcessError:
            # Debian/Ubuntu ship Python without ensurepip unless python3-venv is installed:
            # create the environment without pip and bootstrap pip from PyPA.
            say("python -m venv failed (no ensurepip?); creating it without pip and bootstrapping pip")
            subprocess.run([sys.executable, "-m", "venv", "--without-pip", str(venv)], check=True)
    if not can_import(python, "pip"):
        say(f"downloading {GET_PIP_URL}")
        get_pip = venv / "get-pip.py"
        with urllib.request.urlopen(GET_PIP_URL, timeout=60) as response:  # noqa: S310 - fixed https URL
            get_pip.write_bytes(response.read())
        subprocess.run([str(python), str(get_pip)], check=True)
        get_pip.unlink()
    if not can_import(python, "PySide6"):
        say("installing PySide6 (Qt for Python); this downloads about 200 MB and takes a few minutes")
        subprocess.run([str(python), "-m", "pip", "install", *REQUIREMENTS], check=True)
    return python


def find_python() -> Path:
    """An interpreter with PySide6: the current one, THUNDER_PYTHON, the repository's .venv, or a new .venv."""
    override = os.environ.get("THUNDER_PYTHON")
    if override:
        return Path(override)
    if can_import(sys.executable, "PySide6"):
        return Path(sys.executable)
    existing = venv_python()
    if existing.exists() and can_import(existing, "PySide6"):
        return existing
    if os.environ.get("THUNDER_NO_VENV") == "1":
        raise RuntimeError(f"PySide6 is not installed for {sys.executable} and THUNDER_NO_VENV=1 forbids creating {VENV}")
    return create_venv()


def ensure_python(argv: list[str] | None = None) -> Path:
    """Returns an interpreter with PySide6; if it is not the current one, re-executes the calling
    script with it (and the given arguments) and exits with its status."""
    # (The paths are compared as written: a virtual environment's python is usually a symbolic
    # link to the system interpreter, so resolving them would make the two look identical.)
    if os.environ.get("THUNDER_PYTHON") == sys.executable or can_import(sys.executable, "PySide6"):
        return Path(sys.executable)
    python = find_python()
    command = [str(python), *sys.argv] if argv is None else [str(python), sys.argv[0], *argv]
    say(f"using {python}")
    env = dict(os.environ)
    env["THUNDER_PYTHON"] = str(python)  # the child must not look again
    sys.exit(subprocess.run(command, env=env).returncode)


if __name__ == "__main__":
    print(find_python())
