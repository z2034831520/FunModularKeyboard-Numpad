param(
    [string]$FallbackProject = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$taskName = 'FunModularKeyboard Codex Bridge'
$codexRoot = Join-Path $env:USERPROFILE '.codex'
$installRoot = Join-Path $codexRoot 'keyboard-bridge'
$globalHooksPath = Join-Path $codexRoot 'hooks.json'
$globalHooksSource = Join-Path $PSScriptRoot 'codex_global_hooks.json'

if (-not (Test-Path -LiteralPath $FallbackProject -PathType Container)) {
    throw "Fallback project does not exist: $FallbackProject"
}
if (Test-Path -LiteralPath $globalHooksPath) {
    $installedHooks = Get-Content -Raw -LiteralPath $globalHooksPath
    $expectedHooks = Get-Content -Raw -LiteralPath $globalHooksSource
    if ($installedHooks -ne $expectedHooks) {
        throw "Existing user hooks were not overwritten: $globalHooksPath"
    }
}

New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'codex_bridge.py') -Destination $installRoot -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'codex_status_hook.py') -Destination $installRoot -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'codex_tasks.json') -Destination $installRoot -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'requirements-codex-bridge.txt') -Destination $installRoot -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'start_codex_bridge.ps1') -Destination $installRoot -Force
Copy-Item -LiteralPath $globalHooksSource -Destination $globalHooksPath -Force

$startScript = Join-Path $installRoot 'start_codex_bridge.ps1'
$taskArguments = '-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "{0}" -FallbackProject "{1}"' -f $startScript, $FallbackProject
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $taskArguments
$trigger = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero)
Register-ScheduledTask `
    -TaskName $taskName `
    -Action $action `
    -Trigger $trigger `
    -Settings $settings `
    -Description 'Starts the global FunModularKeyboard Codex bridge at user logon.' `
    -Force | Out-Null

Write-Output "Installed bridge: $installRoot"
Write-Output "Installed user hooks: $globalHooksPath"
Write-Output "Registered startup task: $taskName"
