# Runtime tools

`tll_coop_runtime.dll` is the injected co-op implementation. Its code is in
`src/coop_runtime.cpp`; reverse-engineered RVAs and field offsets live in
`src/coop_runtime_offsets.h`, and packet layouts live in `src/coop_protocol.h`.
There is one DLL target only.

Build every target with `build-release.cmd`. Launch and inject one or two game
instances with `scripts/launch_coop.ps1 -Count 1|2`; the script uses
`tll_dll_injector.exe` and the default `tll_coop_runtime.dll`.

The other executables are external research tools whose target and source names
state their purpose: object scanning, player-position nudging, buddy-teleport
toggling, runtime code dumping, net-player probing, memory reading, dword
scanning/writing and input-slot inspection. `build-memory-tools.cmd` rebuilds the
input-slot dump and dword scanner only.
