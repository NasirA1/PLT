#!/usr/bin/env bash
# Builds the Thunder toolchain (if needed), sets up the Python environment for the IDE (PySide6 in
# .venv, first run only) and starts the Thunder IDE.
# Usage: ./run.sh [--no-build] [file.th]
set -euo pipefail
cd "$(dirname "$0")"
exec "${PYTHON:-python3}" tools/launch.py "$@"
