param(
    [ValidateRange(1, 2)]
    [int]$Count = 1,
    [string]$GamePath = "C:\UNCHARTED Legacy of Thieves Collection (2016-2017-2022)\Uncharted Legacy of Thieves Collection\tll.exe",
    [int]$SettleSeconds = 14,
    [ValidateRange(0, 30)]
    [int]$InjectionGapSeconds = 5,
    [switch]$LocalPair,
    [switch]$EnemyRootSync
)

$ErrorActionPreference = "Stop"
$buildPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\scanner\build"))
$injectorPath = Join-Path $buildPath "tll_dll_injector.exe"

if ($LocalPair -and $Count -ne 2) {
    throw "LocalPair requires Count=2"
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
$processes = @()
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
    $process = [Diagnostics.Process]::Start($startInfo)
    Start-Sleep -Seconds 2
    if ($process.HasExited) {
        throw "Instance $instance exited during startup"
    }
    $processes += $process
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
        # LoadLibrary returns before the runtime's worker thread has finished
        # installing its thread-suspended callsite relays. Starting the second
        # worker immediately can make both processes suspend/audit at once and
        # causes one copy to skip otherwise valid relays. Serialize their cold
        # initialization; zero remains available for explicit stress tests.
        if ($index + 1 -lt $processes.Count -and $InjectionGapSeconds -gt 0) {
            Write-Host "waiting $InjectionGapSeconds seconds for PID=$($process.Id) runtime init"
            Start-Sleep -Seconds $InjectionGapSeconds
        }
    }
} finally {
    Pop-Location
}

$pidList = ($processes | ForEach-Object Id) -join ", "
Write-Host "DONE PIDs=$pidList"
