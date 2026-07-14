param(
    [ValidateRange(1, 2)]
    [int]$Count = 1,
    [string]$GamePath = "C:\Program Files (x86)\Steam\steamapps\common\Uncharted Legacy of Thieves Collection\tll.exe",
    [int]$SettleSeconds = 14
)

$ErrorActionPreference = "Stop"
$buildPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\scanner\build"))
$injectorPath = Join-Path $buildPath "tll_injector.exe"

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
    foreach ($process in $processes) {
        if ($process.HasExited) {
            throw "tll.exe PID=$($process.Id) exited before injection"
        }
        & $injectorPath --pid $process.Id
        if ($LASTEXITCODE -ne 0) {
            throw "Injection failed for PID=$($process.Id) (exit $LASTEXITCODE)"
        }
    }
} finally {
    Pop-Location
}

$pidList = ($processes | ForEach-Object Id) -join ", "
Write-Host "DONE PIDs=$pidList"
