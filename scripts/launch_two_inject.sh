#!/usr/bin/env bash
# Launch TWO tll.exe instances and inject the teleport hook DLL into both.
# Uses PowerShell Start-Process (reliable PIDs). The old cmd.exe /c start path
# hung under WSL and never returned, so instance 2 never launched.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WIN_SCRIPT="$(wslpath -w "$SCRIPT_DIR/launch_inject.ps1")"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$WIN_SCRIPT" -Count 2 "$@"