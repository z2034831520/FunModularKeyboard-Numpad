param(
    [Parameter(Mandatory = $true)]
    [string]$FallbackProject
)

$ErrorActionPreference = 'Stop'
$installRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$bridgePath = Join-Path $installRoot 'codex_bridge.py'
$logRoot = Join-Path $installRoot 'logs'
$logPath = Join-Path $logRoot 'codex_bridge.log'
$pythonExe = (& py -c 'import sys; print(sys.executable)').Trim()

$existing = Get-CimInstance Win32_Process | Where-Object {
    $_.Name -eq 'python.exe' -and
    $_.CommandLine -like "*$bridgePath*"
}
if ($existing) {
    Write-Output "Codex keyboard bridge is already running (PID $($existing[0].ProcessId))."
    exit 0
}

New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
"[$(Get-Date -Format o)] Starting Codex keyboard bridge." | Add-Content -LiteralPath $logPath -Encoding utf8

# Keep Python attached to this PowerShell process. Task Scheduler can now see
# failures and apply its restart policy instead of losing an unsupervised child.
try {
    # Windows PowerShell 5.1 wraps redirected native stderr as ErrorRecord.
    # A recoverable Python warning must not terminate the supervised bridge.
    $ErrorActionPreference = 'Continue'
    & $pythonExe -u $bridgePath --project $FallbackProject --no-open-app 2>&1 | ForEach-Object {
        Add-Content -LiteralPath $logPath -Value $_ -Encoding utf8 -ErrorAction Stop
    }
    $bridgeExitCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
}
catch {
    $ErrorActionPreference = 'Stop'
    $_ | Out-String | Add-Content -LiteralPath $logPath -Encoding utf8
    $bridgeExitCode = 1
}

"[$(Get-Date -Format o)] Bridge exited with code $bridgeExitCode." | Add-Content -LiteralPath $logPath -Encoding utf8
exit $bridgeExitCode
