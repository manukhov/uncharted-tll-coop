#!/usr/bin/env bash
# Launch tll.exe directly (Steam must be running) and inject the teleport hook DLL.
set -e
GAME="/mnt/c/Program Files (x86)/Steam/steamapps/common/Uncharted Legacy of Thieves Collection/tll.exe"
BUILD="/mnt/c/Users/Vladimir/Desktop/TLLCoopResearch/scanner/build"
( cd "$(dirname "$GAME")" && cmd.exe /c start "" "tll.exe" ) >/dev/null 2>&1
echo "launching tll.exe..."
PID=""
for i in $(seq 1 40); do
  sleep 2
  PID=$(tasklist.exe 2>/dev/null | grep -i "tll.exe" | awk '{print $2}' | head -1 | tr -d '\r')
  [ -n "$PID" ] && break
done
[ -z "$PID" ] && { echo "tll.exe did not start"; exit 1; }
echo "tll.exe PID=$PID; waiting for it to settle before inject..."
sleep 12
( cd "$BUILD" && ./tll_injector.exe --pid "$PID" )
echo "PID=$PID"
