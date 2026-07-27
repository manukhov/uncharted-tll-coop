param(
    [ValidateRange(1, 2)]
    [int]$Count = 1,
    [string]$GamePath = "C:\Program Files (x86)\Steam\steamapps\common\Uncharted Legacy of Thieves Collection\tll.exe",
    [int]$SettleSeconds = 14,
    [ValidateRange(0, 30)]
    [int]$InjectionGapSeconds = 5
)

$ErrorActionPreference = "Stop"
$buildPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\scanner\build"))
$injectorPath = Join-Path $buildPath "tll_dll_injector.exe"

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
    $process = Start-Process -FilePath $GamePath -WorkingDirectory $gameDirectory -PassThru
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
