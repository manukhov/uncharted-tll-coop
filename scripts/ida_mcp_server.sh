#!/usr/bin/env bash
# Shared headless IDA MCP server (idalib) for Claude Code + Codex.
#
# Idempotent launcher: starts a persistent HTTP/SSE MCP server (Windows Python 3.12
# + idalib) bound to the WSL vEthernet adapter so both WSL agents can reach it. It
# serves a COPY of the IDB (ida/tll_mcp.i64) so the shared ida/tll.i64 stays free for
# headless idat / label application.
#
# Usage:
#   scripts/ida_mcp_server.sh            # start if not already running
#   scripts/ida_mcp_server.sh --refresh  # re-copy tll.i64 -> tll_mcp.i64 and restart
#   scripts/ida_mcp_server.sh --stop      # stop the server
#
# Endpoint (both clients point here): http://<wsl-host>:8745/mcp
set -uo pipefail
ROOT="/mnt/c/Users/Vladimir/Desktop/TLLCoopResearch"
WIN_ROOT_FWD='C:/Users/Vladimir/Desktop/TLLCoopResearch'
PORT=8745
HOST="$(ip route 2>/dev/null | awk '/default/{print $3; exit}')"
[ -n "$HOST" ] || HOST="172.21.144.1"
COPY="$ROOT/ida/tll_mcp.i64"
WIN_COPY_FWD="$WIN_ROOT_FWD/ida/tll_mcp.i64"
WIN_WRAP_FWD="$WIN_ROOT_FWD/scripts/ida_mcp_server.py"

port_pid() { netstat.exe -ano 2>/dev/null | tr -d '\r' | awk -v p=":$PORT" '$2 ~ p"$" && $4=="LISTENING"{print $5; exit}'; }
is_up() { local c; c="$(curl -s -o /dev/null -w '%{http_code}' -m 3 -H "Host: $HOST:$PORT" "http://$HOST:$PORT/mcp" 2>/dev/null)"; [ -n "$c" ] && [ "$c" != "000" ]; }
stop_server() { local pid; pid="$(port_pid)"; if [ -n "$pid" ]; then taskkill.exe /PID "$pid" /F >/dev/null 2>&1 && echo "stopped server pid=$pid"; fi; }

case "${1:-}" in
  --stop) stop_server; exit 0 ;;
  --refresh)
    stop_server; sleep 2
    echo "refreshing IDB copy (ida/tll.i64 -> tll_mcp.i64)..."
    cp "$ROOT/ida/tll.i64" "$COPY" ;;
esac

if is_up; then echo "IDA MCP already running at http://$HOST:$PORT/mcp"; exit 0; fi
[ -f "$COPY" ] || cp "$ROOT/ida/tll.i64" "$COPY"

echo "starting IDA MCP server on http://$HOST:$PORT/mcp (loading IDB, ~1-2 min)..."
powershell.exe -NoProfile -Command \
  "Start-Process -FilePath 'py.exe' -ArgumentList '-3.12','$WIN_WRAP_FWD','--host','$HOST','--port','$PORT','$WIN_COPY_FWD' -WorkingDirectory '$WIN_ROOT_FWD' -WindowStyle Hidden" \
  >/dev/null 2>&1
echo "launched. Poll readiness: scripts/ida_mcp_server.sh (re-run prints 'already running' once ready)."
echo "endpoint: http://$HOST:$PORT/mcp"
