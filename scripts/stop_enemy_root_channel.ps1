param(
    [Parameter(Mandatory)] [int[]]$ProcessIds,
    [switch]$InspectOnly
)
$ErrorActionPreference = 'Stop'
# New runtime control only: existing PID+raw-creation-time events. No arbitrary
# process memory writes, PDB offsets, game input, rearm, or legacy hash bypass.
if (-not ('EnemyRootChannelControl' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class EnemyRootChannelControl {
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint rights, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetProcessTimes(IntPtr h, out long created, out long exited, out long kernel, out long user);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr OpenEvent(uint rights, bool inherit, string name);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool SetEvent(IntPtr h);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
'@
}
$pending = @()
$failures = @()
# Signal every available target before waiting for ANY acknowledgement. One
# stalled copy must not prevent the other copy from receiving its stop request.
foreach ($targetId in ($ProcessIds | Select-Object -Unique)) {
    $handle = $request = $ack = [IntPtr]::Zero
    $retained = $false
    try {
        $process = Get-Process -Id $targetId
        if ($process.ProcessName -ne 'tll') { throw "PID $targetId is not tll" }
        $handle = [EnemyRootChannelControl]::OpenProcess(0x101000, $false, $targetId)
        if ($handle -eq [IntPtr]::Zero) { throw "Cannot inspect PID $targetId" }
        [long]$created = 0; [long]$exited = 0; [long]$kernel = 0; [long]$userTime = 0
        if (-not [EnemyRootChannelControl]::GetProcessTimes($handle, [ref]$created, [ref]$exited, [ref]$kernel, [ref]$userTime)) {
            throw 'Cannot read process creation time'
        }
        if ($process.StartTime.ToFileTimeUtc() -ne $created -or [EnemyRootChannelControl]::WaitForSingleObject($handle, 0) -ne 258) {
            throw 'Process identity changed or exited'
        }
        $name = 'Local\TllEnemyRootStop_v1_{0}_{1:X16}' -f $targetId, $created
        $requestRights = if ($InspectOnly) { 0x100000 } else { 0x100002 }
        $request = [EnemyRootChannelControl]::OpenEvent($requestRights, $false, $name)
        $ack = [EnemyRootChannelControl]::OpenEvent(0x100000, $false, "${name}_ACK")
        if ($request -eq [IntPtr]::Zero -or $ack -eq [IntPtr]::Zero) { throw 'Runtime stop events unavailable; no fallback memory write' }
        if (-not $InspectOnly -and -not [EnemyRootChannelControl]::SetEvent($request)) { throw 'Stop request failed' }
        $pending += [pscustomobject]@{ Id = $targetId; Process = $handle; Request = $request; Ack = $ack }
        $retained = $true
    } catch {
        $failures += "PID=${targetId}: $($_.Exception.Message)"
    } finally {
        if (-not $retained) {
            if ($request -ne [IntPtr]::Zero) { [void][EnemyRootChannelControl]::CloseHandle($request) }
            if ($ack -ne [IntPtr]::Zero) { [void][EnemyRootChannelControl]::CloseHandle($ack) }
            if ($handle -ne [IntPtr]::Zero) { [void][EnemyRootChannelControl]::CloseHandle($handle) }
        }
    }
}
foreach ($target in $pending) {
    try {
        $waitMs = if ($InspectOnly) { 0 } else { 5000 }
        $state = [EnemyRootChannelControl]::WaitForSingleObject($target.Ack, $waitMs)
        if ($state -ne 0 -and $state -ne 258) { throw 'Acknowledgement wait failed' }
        Write-Output "PID=$($target.Id) inspectOnly=$InspectOnly stoppedAndQuiescent=$($state -eq 0)"
        if (-not $InspectOnly -and $state -ne 0) { throw 'Stop requested but not yet acknowledged; do not assume the writer is quiescent' }
    } catch {
        $failures += "PID=$($target.Id): $($_.Exception.Message)"
    } finally {
        [void][EnemyRootChannelControl]::CloseHandle($target.Request)
        [void][EnemyRootChannelControl]::CloseHandle($target.Ack)
        [void][EnemyRootChannelControl]::CloseHandle($target.Process)
    }
}
if ($failures.Count) { throw ($failures -join "`n") }
