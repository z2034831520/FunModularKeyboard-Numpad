param(
    [string]$FallbackProject = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$taskName = 'FunModularKeyboard Codex Bridge'
$installedRoot = Join-Path $env:USERPROFILE '.codex\keyboard-bridge'
$installedBridge = Join-Path $installedRoot 'codex_bridge.py'
$logPath = Join-Path $installedRoot 'logs\codex_bridge.log'

function Find-CodexBridgeProcess {
    @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -in @('python.exe', 'pythonw.exe') -and
        $_.CommandLine -like '*codex_bridge.py*'
    })
}

$running = Find-CodexBridgeProcess
if ($running.Count -gt 0) {
    Write-Output "Codex keyboard bridge is already running (PID $($running[0].ProcessId))."
    exit 0
}

if (-not (Test-Path -LiteralPath $installedBridge -PathType Leaf)) {
    throw "Bridge is not installed. Run software\install_codex_bridge.ps1 first."
}

$task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($null -eq $task) {
    throw "Scheduled task '$taskName' is missing. Run software\install_codex_bridge.ps1 first."
}

Start-ScheduledTask -TaskName $taskName
$deadline = [DateTime]::UtcNow.AddSeconds(12)
do {
    Start-Sleep -Milliseconds 250
    $running = Find-CodexBridgeProcess
} while ($running.Count -eq 0 -and [DateTime]::UtcNow -lt $deadline)

if ($running.Count -eq 0) {
    $details = if (Test-Path -LiteralPath $logPath) {
        (Get-Content -LiteralPath $logPath -Tail 12 -ErrorAction SilentlyContinue) -join [Environment]::NewLine
    }
    else {
        'No bridge log was created.'
    }
    throw "Bridge did not stay running. Log: $logPath`n$details"
}

Write-Output "Codex keyboard bridge started (PID $($running[0].ProcessId))."
Write-Output "Log: $logPath"
