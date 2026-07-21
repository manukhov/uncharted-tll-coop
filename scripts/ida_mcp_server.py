"""Headless IDA MCP server (idalib) for this workspace.

Runs on Windows Python 3.12 (with idalib activated), binds the WSL vEthernet
adapter, and opens the workspace's only IDB: ida/tll.i64. The launcher must stop
this process before headless idat applies labels, then start it again after the
database is saved. Launched by scripts/ida_mcp_server.ps1.

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

# mcp-plugin.py wraps every tool in idaapi.execute_sync(fn, MFF_*), meant for the GUI
# plugin's UI thread. Headless idalib is single-threaded, so run the callable inline.
# (idaapi is available now that `import idapro` initialized idalib.)
import idaapi  # noqa: E402


def _direct_execute_sync(callable_, _reqf):
    callable_()
    return 0


idaapi.execute_sync = _direct_execute_sync

# idalib_server.main() hardcodes transport="sse", but the configured client uses
# streamable-http. Force it here (endpoint becomes /mcp). Intercept the run()
# call so we keep all of main()'s DB-open / tool-registration logic.
_orig_run = idalib_server.mcp.run
idalib_server.mcp.run = lambda *a, **k: _orig_run(transport="streamable-http")

# THE headless decompile fix: mcp-plugin.decompile_function calls is_window_active(),
# which does `from PyQt5.QtWidgets import QApplication`. Headless, IDA's PyQt5 shim
# raises NotImplementedError("Can't import PySide6. Are you trying to use Qt without
# GUI?"), but is_window_active only catches ImportError, so it propagates and breaks
# decompile. Force it to False (there is no GUI window headless). Pre-import the plugin
# (idapro already initialized idalib) so the patch is in place before main() registers
# tools from the same cached module.
import importlib  # noqa: E402

_plugin = importlib.import_module("ida_pro_mcp.mcp-plugin")
_plugin.is_window_active = lambda: False

idalib_server.main()
