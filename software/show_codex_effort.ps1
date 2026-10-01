param(
    [switch]$Watch,
    [string]$ReportPath = (Join-Path $env:USERPROFILE '.codex\keyboard-bridge\effort-state.json')
)

do {
    try {
        $report=Get-Content -Raw -Encoding UTF8 -LiteralPath $ReportPath -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        [pscustomobject]@{
            SelectedModel=$report.selected_model
            SelectedEffort=$report.selected_effort
            LocalEffectiveEffort=$report.local_effective_effort
            GuiPickerConfirmedEffort=$report.gui_picker_confirmed_effort
            GuiStatus=$report.desktop.status
            Reason=$report.desktop.reason
            SavedHostHint=$report.desktop.saved_host_hint
            RuntimeVerified=$report.task_runtime_effort_verified
            ReportUpdatedAt=[DateTimeOffset]::FromUnixTimeSeconds([long]$report.updated_at).LocalDateTime
        } | Format-List
    }
    catch {Write-Output 'Effort report unavailable. Start the updated bridge first.'}
    if($Watch){Start-Sleep -Seconds 2}
} while($Watch)
