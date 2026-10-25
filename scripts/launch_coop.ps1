param(
    [ValidateRange(1, 2)]
    [int]$Count = 1,
    [string]$GamePath = "C:\UNCHARTED Legacy of Thieves Collection (2016-2017-2022)\Uncharted Legacy of Thieves Collection\tll.exe",
    [int]$SettleSeconds = 14,
    [ValidateRange(0, 30)]
    [int]$InjectionGapSeconds = 5,
    [ValidateRange(1, 60)]
    [int]$BootReadyTimeoutSeconds = 45,
    [switch]$LocalPair,
    [switch]$EnemyRootSync,
    [switch]$EnemyCombatSync
)

$ErrorActionPreference = "Stop"
$buildPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\scanner\build"))
$injectorPath = Join-Path $buildPath "tll_dll_injector.exe"

if ($LocalPair -and $Count -ne 2) {
    throw "LocalPair requires Count=2"
}
if ($EnemyCombatSync -and -not $EnemyRootSync) {
    throw 'EnemyCombatSync requires EnemyRootSync; no separate legacy damage relay'
}

function Wait-CoopBootstrap {
    param([Diagnostics.Process]$Process, [long]$Created)
    $stem = 'Local\TllCoopBoot_v1_{0}_{1:X16}' -f $Process.Id, [uint64]$Created
    $events = @{}
    $timer = [Diagnostics.Stopwatch]::StartNew()
    try {
        while ($timer.Elapsed.TotalSeconds -lt $BootReadyTimeoutSeconds) {
            $Process.Refresh()
            if ($Process.HasExited) { throw "PID=$($Process.Id) exited before co-op bootstrap readiness (exit=$($Process.ExitCode))" }
            if ($Process.StartTime.ToFileTimeUtc() -ne $Created) { throw "PID=$($Process.Id) creation changed" }
            # A refusal wins if an inconsistent producer ever signals both.
            foreach ($kind in 'REFUSED', 'READY') {
                if (-not $events.ContainsKey($kind)) {
                    [Threading.EventWaitHandle]$opened = $null
                    if ([Threading.EventWaitHandle]::TryOpenExisting("${stem}_$kind", [ref]$opened)) {
                        $events[$kind] = $opened
                    }
                }
                if ($events.ContainsKey($kind) -and $events[$kind].WaitOne(0)) {
                    if ($kind -eq 'REFUSED') { throw "PID=$($Process.Id) COOP-BOOT REFUSED; see preserved native installer stages" }
                    Write-Output "COOP-BOOT READY PID=$($Process.Id) created=$Created (bootstrap only; scene pairing/NPC behavior pending)"
                    return
                }
            }
            Start-Sleep -Milliseconds 100
        }
        throw "PID=$($Process.Id) co-op bootstrap timed out; LoadLibrary alone is not readiness"
    } finally {
        foreach ($event in $events.Values) { $event.Dispose() }
    }
}

function Stop-OwnedLaunchCopies {
    param([object[]]$Processes, [hashtable]$Identities, [string]$ExpectedImage)
    foreach ($owned in $Processes) {
        try {
            $owned.Refresh()
            if ($owned.HasExited) {
                Write-Output "PID=$($owned.Id) already exited (exit=$($owned.ExitCode)); not classified as a crash"
                continue
            }
            if (-not $Identities.ContainsKey($owned.Id) -or
                $owned.StartTime.ToFileTimeUtc() -ne $Identities[$owned.Id] -or
                [IO.Path]::GetFullPath($owned.MainModule.FileName) -ne $ExpectedImage) {
                Write-Warning "Do not close PID=$($owned.Id): executable/creation identity mismatch"
                continue
            }
            # Original Process.Start object retains the exact process handle.
            # No process-name kill, game input or native menu action is used.
            $owned.Kill()
            [void]$owned.WaitForExit(5000)
            Write-Output "Intentionally closed exact launched PID=$($owned.Id) after bootstrap failure; not a menu-exit result"
        } catch {
            Write-Warning "Could not close exact launched PID=$($owned.Id): $($_.Exception.Message)"
        }
    }
}

# Do not relaunch the rejected auto-enabled image while an updated build is
# pending. Running instances have been stopped through their terminal flag.
$runtimePath = Join-Path $buildPath 'tll_coop_runtime.dll'
if ((Test-Path -LiteralPath $runtimePath -PathType Leaf) -and
    (Get-FileHash -LiteralPath $runtimePath -Algorithm SHA256).Hash -eq
        'E19D25ECD090FF5233AE1522C39A46882422F3858FE5AC117C438F271BC93EB5') {
    throw 'Rejected enemy-sync image: close the current games and build the default-disabled runtime before launching again'
}

if (-not (Test-Path -LiteralPath $GamePath -PathType Leaf)) {
    throw "Game executable not found: $GamePath"
}
if (-not (Test-Path -LiteralPath $injectorPath -PathType Leaf)) {
    throw "Injector not found: $injectorPath"
}

$gameDirectory = [IO.Path]::GetDirectoryName($GamePath)
$expectedImage = [IO.Path]::GetFullPath($GamePath)
$processes = @()
$identities = @{}
try {
for ($instance = 1; $instance -le $Count; $instance++) {
    Write-Host "== launching instance $instance =="
    # Per-process A/B roles: do not rewrite the shared user's coop_role.txt.
    # Set 0 explicitly on ordinary launches, even if the parent inherited 1.
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $GamePath
    $startInfo.WorkingDirectory = $gameDirectory
    $startInfo.UseShellExecute = $false
    if ($LocalPair) { $startInfo.EnvironmentVariables['TLL_COOP_ROLE'] = [string]$instance }
    $startInfo.EnvironmentVariables['TLL_ENEMY_ROOT_SYNC'] = if ($EnemyRootSync) { '1' } else { '0' }
    $startInfo.EnvironmentVariables['TLL_ENEMY_COMBAT_SYNC'] = if ($EnemyCombatSync) { '1' } else { '0' }
    $process = [Diagnostics.Process]::Start($startInfo)
    $processes += $process
    $identities[$process.Id] = $process.StartTime.ToFileTimeUtc()
    Start-Sleep -Seconds 2
    if ($process.HasExited) {
        throw "Instance $instance exited during startup"
    }
    Write-Host "instance $instance PID=$($process.Id)"
}

Write-Host "== settling for $SettleSeconds seconds, then injecting =="
Start-Sleep -Seconds $SettleSeconds
Push-Location $buildPath
try {
    for ($index = 0; $index -lt $processes.Count; $index++) {
        $process = $processes[$index]
        if ($process.HasExited) {
            throw "tll.exe PID=$($process.Id) exited before injection"
        }
        & $injectorPath --pid $process.Id
        if ($LASTEXITCODE -ne 0) {
            throw "Injection failed for PID=$($process.Id) (exit $LASTEXITCODE)"
        }
        if ($LocalPair -or $EnemyRootSync) {
            Wait-CoopBootstrap -Process $process -Created $identities[$process.Id]
        }
        # LoadLibrary returns before installer completion; the readiness wait
        # above supplies the actual ordering for co-op pairs. Retain the legacy
        # optional gap without claiming it explains a cross-process failure:
        # each suspend helper filters threads by its own process identity.
        if ($index + 1 -lt $processes.Count -and $InjectionGapSeconds -gt 0) {
            Write-Host "waiting $InjectionGapSeconds seconds for PID=$($process.Id) runtime init"
            Start-Sleep -Seconds $InjectionGapSeconds
        }
    }
} finally {
    Pop-Location
}

foreach ($process in $processes) {
    # Earlier copies can exit while a later bootstrap is still being installed.
    # Revalidate the complete pair, not only the most recently waited process.
    $process.Refresh()
    if ($process.HasExited -or $process.StartTime.ToFileTimeUtc() -ne $identities[$process.Id] -or
        [IO.Path]::GetFullPath($process.MainModule.FileName) -ne $expectedImage) {
        throw "Launched PID=$($process.Id) is no longer the expected live game; pair is not ready"
    }
}
$pidList = ($processes | ForEach-Object Id) -join ", "
Write-Host "DONE PIDs=$pidList"
} catch {
    $launchFailure = $_
    # Preserve evidence BEFORE stopping this exact newly-created pair. These
    # are ignored local artifacts, not a new game observer or retry mechanism.
    try {
        $failureRoot = Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\reports'))) (
            'bootstrap-refused-' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmssfffZ'))
        New-Item -ItemType Directory -Path $failureRoot -ErrorAction Stop | Out-Null
        $sharedLog = Join-Path $PSScriptRoot '..\reports\locomotion_hook.log'
        if (Test-Path -LiteralPath $sharedLog -PathType Leaf) {
            Copy-Item -LiteralPath $sharedLog -Destination (Join-Path $failureRoot 'locomotion_hook.log')
        }
        foreach ($owned in $processes) {
            $privateLog = Join-Path $PSScriptRoot "..\reports\enemy_root_$($owned.Id).log"
            if (Test-Path -LiteralPath $privateLog -PathType Leaf) {
                Copy-Item -LiteralPath $privateLog -Destination $failureRoot
            }
        }
        Write-Output "Bootstrap failure evidence: $failureRoot"
    } catch { Write-Warning "Could not preserve bootstrap failure logs: $($_.Exception.Message)" }
    Stop-OwnedLaunchCopies -Processes $processes -Identities $identities -ExpectedImage $expectedImage
    throw $launchFailure
}
