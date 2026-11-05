#!/usr/bin/env bash
# Builds the Thunder toolchain (if needed) and starts the Thunder IDE.
# Usage: ./run.sh [--no-build] [file.th]
set -euo pipefail
cd "$(dirname "$0")"
exec "${PYTHON:-python3}" tools/launch.py "$@"
