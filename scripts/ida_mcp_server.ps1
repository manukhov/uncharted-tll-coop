[CmdletBinding()]
param(
    [ValidateSet('Start', 'Stop', 'Restart', 'Status')]
    [string]$Action = 'Start'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$databasePath = Join-Path $repoRoot 'ida\tll.i64'
$databaseDirectory = Split-Path -Parent $databasePath
$wrapperPath = Join-Path $PSScriptRoot 'ida_mcp_server.py'
$port = 8745
$hostAddress = Get-NetIPAddress -AddressFamily IPv4 -InterfaceAlias 'vEthernet (WSL)' `
    -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty IPAddress
if (-not $hostAddress) {
    # WSL is optional. Local Codex/PowerShell clients use the loopback endpoint
    # when the WSL adapter is absent or temporarily disabled.
    $hostAddress = '127.0.0.1'
}

function Get-IdaMcpProcesses {
    Get-CimInstance Win32_Process | Where-Object {
        $_.Name -in @('py.exe', 'python.exe', 'python3.exe') -and
        $_.CommandLine -like '*ida_mcp_server.py*'
    }
}

function Stop-IdaMcpServer {
    $processes = @(Get-IdaMcpProcesses)
    foreach ($process in $processes) {
        Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
    }
    if ($processes.Count -gt 0) {
        Start-Sleep -Seconds 1
        Write-Output "Stopped IDA MCP processes: $($processes.ProcessId -join ', ')"
    } else {
        Write-Output 'IDA MCP is not running.'
    }

    # idalib expands the single .i64 into disposable working components while
    # the server is open. MCP writes are forbidden, so nothing in these files
    # needs to survive after every server process has stopped.
    $workingFiles = @(
        'tll.$$$', 'tll.id0', 'tll.id1', 'tll.id2', 'tll.nam', 'tll.til'
    )
    foreach ($name in $workingFiles) {
        $path = Join-Path $databaseDirectory $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Remove-Item -LiteralPath $path -Force
        }
    }
}

function Get-IdaMcpListener {
    Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue |
        Select-Object -First 1
}

if ($Action -in @('Stop', 'Restart')) {
    Stop-IdaMcpServer
    if ($Action -eq 'Stop') {
        return
    }
}

if ($Action -eq 'Status') {
    $listener = Get-IdaMcpListener
    if ($listener) {
        Write-Output "IDA MCP is listening at http://${hostAddress}:${port}/mcp (PID $($listener.OwningProcess))."
    } elseif (Get-IdaMcpProcesses) {
        Write-Output 'IDA MCP is loading the database but is not listening yet.'
    } else {
        Write-Output 'IDA MCP is stopped.'
    }
    return
}

if (Get-IdaMcpProcesses) {
    Write-Output "IDA MCP is already running or loading at http://${hostAddress}:${port}/mcp."
    return
}
if (-not (Test-Path -LiteralPath $databasePath -PathType Leaf)) {
    throw "The IDB does not exist: $databasePath"
}

$arguments = @(
    '-3.12',
    ('"{0}"' -f $wrapperPath),
    '--host', $hostAddress,
    '--port', $port,
    ('"{0}"' -f $databasePath)
)
Start-Process -FilePath 'py.exe' -ArgumentList $arguments -WorkingDirectory $repoRoot -WindowStyle Hidden
Start-Sleep -Seconds 1
if (-not (Get-IdaMcpProcesses)) {
    throw 'IDA MCP exited before it could load the database.'
}
Write-Output "Started IDA MCP loading the single IDB at http://${hostAddress}:${port}/mcp."
