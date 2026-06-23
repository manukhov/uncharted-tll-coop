#!/usr/bin/env bash
# Launch TWO tll.exe instances and inject the teleport hook DLL into both.
# One-PC two-instance co-op test (Steam allows direct-launch of two instances).
# Net auto-roles on loopback: 1st binds 31338->sends 31339; 2nd binds 31339->sends
# 31338 (coop_peer.txt = 127.0.0.1).
set -e
GAME="/mnt/c/Program Files (x86)/Steam/steamapps/common/Uncharted Legacy of Thieves Collection/tll.exe"
BUILD="/mnt/c/Users/Vladimir/Desktop/TLLCoopResearch/scanner/build"

pids() { tasklist.exe 2>/dev/null | grep -i "tll.exe" | awk '{print $2}' | tr -d '\r'; }

launch_one() {
  ( cd "$(dirname "$GAME")" && cmd.exe /c start "" "tll.exe" ) >/dev/null 2>&1
}

wait_new_pid() { # arg = space-separated list of already-known pids
  local known="$1" newpid=""
  for i in $(seq 1 40); do
    sleep 2
    for p in $(pids); do
      case " $known " in *" $p "*) ;; *) newpid="$p";; esac
    done
    [ -n "$newpid" ] && break
  done
  echo "$newpid"
}

echo "== launching instance 1 =="
launch_one
PID1="$(wait_new_pid "")"
[ -z "$PID1" ] && { echo "instance 1 did not start"; exit 1; }
echo "instance 1 PID=$PID1"

echo "== launching instance 2 =="
launch_one
PID2="$(wait_new_pid "$PID1")"
[ -z "$PID2" ] && { echo "instance 2 did not start"; exit 1; }
echo "instance 2 PID=$PID2"

echo "== settling, then injecting both =="
sleep 14
( cd "$BUILD" && ./tll_injector.exe --pid "$PID1" )
( cd "$BUILD" && ./tll_injector.exe --pid "$PID2" )
echo "DONE PID1=$PID1 PID2=$PID2"
