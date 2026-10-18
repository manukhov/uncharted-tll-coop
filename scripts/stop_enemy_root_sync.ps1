param(
    [Parameter(Mandatory)] [int[]]$ProcessIds,
    [switch]$InspectOnly
)
$ErrorActionPreference = 'Stop'
$runtimePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\scanner\build\tll_coop_runtime.dll'))
# This emergency stop is restricted to the first deployed root-sync image.
# It sets only that image's one-byte terminal stop flag, never game data/code.
$expectedHash = 'E19D25ECD090FF5233AE1522C39A46882422F3858FE5AC117C438F271BC93EB5'
if ((Get-FileHash -LiteralPath $runtimePath -Algorithm SHA256).Hash -ne $expectedHash) {
    throw 'Runtime image differs from the admitted emergency-stop build'
}
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class EnemyRootStopNative {
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint rights, bool inherit, int pid);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr h, IntPtr p, byte[] b, UIntPtr n, out UIntPtr done);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool WriteProcessMemory(IntPtr h, IntPtr p, byte[] b, UIntPtr n, out UIntPtr done);
    [DllImport("dbghelp.dll", CharSet=CharSet.Ansi, SetLastError=true)] public static extern bool SymInitialize(IntPtr h, string search, bool invade);
    [DllImport("dbghelp.dll")] public static extern uint SymSetOptions(uint flags);
    [DllImport("dbghelp.dll", CharSet=CharSet.Ansi, SetLastError=true)] public static extern ulong SymLoadModuleEx(IntPtr h, IntPtr file, string image, string module, ulong imageBase, uint size, IntPtr data, uint flags);
    public delegate bool EnumSymbol(IntPtr symbol, uint size, IntPtr context);
    [DllImport("dbghelp.dll", CharSet=CharSet.Ansi, SetLastError=true)] public static extern bool SymEnumSymbols(IntPtr h, ulong imageBase, string mask, EnumSymbol callback, IntPtr context);
    [DllImport("dbghelp.dll")] public static extern bool SymCleanup(IntPtr h);
    public static ulong FindStop(IntPtr h, ulong imageBase) {
        var matches = new List<ulong>();
        EnumSymbol callback = (p, size, context) => {
            string name = Marshal.PtrToStringAnsi(IntPtr.Add(p,84), Marshal.ReadInt32(p,76));
            if (name.Contains("enemy_root") && (name.Contains("::stopped") || name.StartsWith("?stopped@enemy_root@"))) {
                if (Marshal.ReadInt32(p,28) != 1) throw new Exception("Unexpected stop symbol size: " + name);
                matches.Add(unchecked((ulong)Marshal.ReadInt64(p,56)));
            }
            return true;
        };
        if (!SymEnumSymbols(h,imageBase,"*stopped*",callback,IntPtr.Zero)) throw new Exception("Symbol enumeration failed");
        GC.KeepAlive(callback);
        if (matches.Count != 1) throw new Exception("Expected one stop symbol, found " + matches.Count);
        return matches[0];
    }
    public static byte[] Read(IntPtr h, ulong address, int size) {
        byte[] result = new byte[size]; UIntPtr done;
        if (!ReadProcessMemory(h,new IntPtr(unchecked((long)address)),result,(UIntPtr)size,out done) || done.ToUInt64()!=(ulong)size)
            throw new Exception("Remote read failed");
        return result;
    }
}
'@
$disk = [IO.File]::ReadAllBytes($runtimePath)
$pe = [BitConverter]::ToInt32($disk,0x3C)
$stamp = [BitConverter]::ToUInt32($disk,$pe+8)
$imageSize = [BitConverter]::ToUInt32($disk,$pe+24+56)
foreach ($targetId in $ProcessIds) {
    $process = Get-Process -Id $targetId
    if ($process.ProcessName -ne 'tll') { throw "PID $targetId is not tll" }
    $created = $process.StartTime
    $module = @($process.Modules | Where-Object { $_.FileName -eq $runtimePath })
    if ($module.Count -ne 1 -or $module[0].ModuleMemorySize -ne $imageSize) { throw 'Runtime module mismatch' }
    $base = [uint64]$module[0].BaseAddress.ToInt64()
    $handle = [EnemyRootStopNative]::OpenProcess(0x438,$false,$targetId)
    if ($handle -eq [IntPtr]::Zero) { throw 'OpenProcess failed' }
    $symbols = $false
    try {
        $header = [EnemyRootStopNative]::Read($handle,$base,4096)
        if ([BitConverter]::ToInt32($header,0x3C) -ne $pe -or [BitConverter]::ToUInt32($header,$pe+8) -ne $stamp -or
            [BitConverter]::ToUInt32($header,$pe+24+56) -ne $imageSize) { throw 'Loaded PE identity differs' }
        [void][EnemyRootStopNative]::SymSetOptions(0x402) # exact PDB match + undecorated names, local search only
        $symbols = [EnemyRootStopNative]::SymInitialize($handle,[IO.Path]::GetDirectoryName($runtimePath),$false)
        if (-not $symbols) { throw 'SymInitialize failed' }
        $loaded = [EnemyRootStopNative]::SymLoadModuleEx($handle,[IntPtr]::Zero,$runtimePath,'tll_coop_runtime',$base,$imageSize,[IntPtr]::Zero,0)
        if ($loaded -ne $base) { throw 'Matching runtime symbols unavailable' }
        $stop = [EnemyRootStopNative]::FindStop($handle,$base)
        if ($stop -lt $base -or $stop -ge $base+$imageSize) { throw 'Stop symbol outside runtime image' }
        $before = [EnemyRootStopNative]::Read($handle,$stop,1)[0]
        if ($before -gt 1 -or (Get-Process -Id $targetId).StartTime -ne $created) { throw 'Process or stop state changed' }
        if (-not $InspectOnly) {
            $written = [UIntPtr]::Zero
            if (-not [EnemyRootStopNative]::WriteProcessMemory($handle,[IntPtr]([long]$stop),[byte[]]@(1),[UIntPtr]1,[ref]$written) -or $written.ToUInt64() -ne 1) {
                throw 'One-byte terminal stop failed'
            }
        }
        $after = [EnemyRootStopNative]::Read($handle,$stop,1)[0]
        if (-not $InspectOnly -and $after -ne 1) { throw 'Terminal stop verification failed' }
        Write-Output "PID=$targetId stopRVA=$('{0:X}' -f ($stop-$base)) before=$before after=$after inspectOnly=$InspectOnly"
    } finally {
        if ($symbols) { [void][EnemyRootStopNative]::SymCleanup($handle) }
        [void][EnemyRootStopNative]::CloseHandle($handle)
    }
}
