#!/usr/bin/env bash
# Query the shared ida-tll MCP server (streamable-http) for a single tool call and
# print the text result. Useful when the MCP tools aren't loaded as native session
# tools (e.g. server came up after session start) but the fast in-memory DB is up.
#
# Usage: scripts/mcp_query.sh <tool_name> '<json-args>'
#   scripts/mcp_query.sh decompile_function '{"address":"0x140D12B30"}'
#   scripts/mcp_query.sh get_xrefs_to '{"address":"0x141C077B0"}'
#   scripts/mcp_query.sh get_callers '{"function_address":"0x140D12B30"}'
set -uo pipefail
HOST="$(ip route 2>/dev/null | awk '/default/{print $3; exit}')"; [ -n "$HOST" ] || HOST="172.21.144.1"
PORT=8745
U="http://$HOST:$PORT/mcp"
H=(-H "Host: $HOST:$PORT" -H "Content-Type: application/json" -H "Accept: application/json, text/event-stream")
TOOL="${1:?tool name required}"
ARGS="${2-}"
[ -n "$ARGS" ] || ARGS='{}'

SID="$(curl -sS -m 8 -D - -o /dev/null -X POST "$U" "${H[@]}" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"cli","version":"1"}}}' \
  2>/dev/null | tr -d '\r' | awk -F': ' 'tolower($1)=="mcp-session-id"{print $2}')"
if [ -z "$SID" ]; then echo "ERROR: no MCP session (server up? scripts/ida_mcp_server.sh)"; exit 1; fi
curl -sS -m 6 -X POST "$U" "${H[@]}" -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc":"2.0","method":"notifications/initialized"}' >/dev/null 2>&1

curl -sS -m 120 -X POST "$U" "${H[@]}" -H "Mcp-Session-Id: $SID" \
  -d "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"$TOOL\",\"arguments\":$ARGS}}" \
  2>/dev/null | tr -d '\r' | sed -n 's/^data: //p' | python3 -c '
import sys, json
for line in sys.stdin:
    try:
        o = json.loads(line)
    except Exception:
        continue
    if "error" in o:
        print("MCP ERROR:", json.dumps(o["error"])); break
    for c in o.get("result", {}).get("content", []):
        print(c.get("text", ""))
'
