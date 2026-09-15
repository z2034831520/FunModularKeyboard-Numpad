param(
    [Parameter(Mandatory = $true)]
    [string]$FallbackProject
)

$ErrorActionPreference = 'Stop'
$installRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$bridgePath = Join-Path $installRoot 'codex_bridge.py'
$pythonExe = (& py -c 'import sys; print(sys.executable)').Trim()

$existing = Get-CimInstance Win32_Process | Where-Object {
    $_.Name -eq 'python.exe' -and
    $_.CommandLine -like "*$bridgePath*"
}
if ($existing) {
    exit 0
}

$argumentLine = '"{0}" --project "{1}" --no-open-app' -f $bridgePath, $FallbackProject
Start-Process `
    -FilePath $pythonExe `
    -ArgumentList $argumentLine `
    -WorkingDirectory $installRoot `
    -WindowStyle Hidden
