#!/usr/bin/env bash
# DibaVault flasher launcher (Linux / macOS) — dibachain
set -e
cd "$(dirname "$0")"
PY=python3
command -v $PY >/dev/null 2>&1 || PY=python
command -v $PY >/dev/null 2>&1 || { echo "Python 3 is required. Install it from https://python.org"; exit 1; }
exec "$PY" dibavault_flash.py "$@"
