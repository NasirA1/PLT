#!/usr/bin/env bash
# Builds Thunder with coverage and sanitizers, runs all C++ and Python tests and prints
# a coverage and code-quality summary. Exits non-zero if anything fails.
# Usage: ./run_tests.sh [--clean] [--tidy]
set -euo pipefail
cd "$(dirname "$0")"
exec "${PYTHON:-python3}" tools/run_tests.py "$@"
