"""Shared headless IDA MCP server (idalib) for Claude Code + Codex.

Runs on Windows Python 3.12 (with idalib activated). Binds the WSL vEthernet
adapter so both WSL-side agents can reach it over HTTP/SSE, and reads a COPY of
the IDB (ida/tll_mcp.i64) so the real ida/tll.i64 stays free for headless idat /
label application. Launched by scripts/ida_mcp_server.sh.

Fix: FastMCP auto-enables a localhost-only Host allowlist at import time (its
default host is 127.0.0.1). We bind a different host, so we widen the allowlist to
include it. The port is firewalled to the WSL-only vEthernet, so this is not a
public exposure.
"""
import sys

from mcp.server.transport_security import TransportSecuritySettings
import ida_pro_mcp.idalib_server as idalib_server


def _host_from_argv(default: str = "127.0.0.1") -> str:
    if "--host" in sys.argv:
        i = sys.argv.index("--host")
        if i + 1 < len(sys.argv):
            return sys.argv[i + 1]
    return default


host = _host_from_argv()
idalib_server.mcp.settings.transport_security = TransportSecuritySettings(
    enable_dns_rebinding_protection=True,
    allowed_hosts=[f"{host}:*", "127.0.0.1:*", "localhost:*"],
    allowed_origins=[
        f"http://{host}:*",
        "http://127.0.0.1:*",
        "http://localhost:*",
    ],
)

# mcp-plugin.py wraps every tool in idaapi.execute_sync(fn, MFF_*), which routes
# through IDA's UI/Qt main loop. Under headless idalib there is no Qt loop, so every
# IDB-touching tool (decompile, xrefs, ...) fails with "Can't import PySide6. Are you
# trying to use Qt without GUI?". Headless is single-threaded, so just run the callable
# inline. (idaapi is available now that `import idapro` initialized idalib.)
import idaapi  # noqa: E402


def _direct_execute_sync(callable_, _reqf):
    callable_()
    return 0


idaapi.execute_sync = _direct_execute_sync

# idalib_server.main() hardcodes transport="sse", but Codex only speaks
# streamable-http (`codex mcp add --url`). Both Claude Code and Codex support
# streamable-http, so force it here (endpoint becomes /mcp). Intercept the run()
# call so we keep all of main()'s DB-open / tool-registration logic.
_orig_run = idalib_server.mcp.run
idalib_server.mcp.run = lambda *a, **k: _orig_run(transport="streamable-http")

idalib_server.main()
