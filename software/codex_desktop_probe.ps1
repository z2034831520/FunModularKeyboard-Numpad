param(
    [ValidateSet('Probe', 'Sync')][string]$Mode = 'Probe',
    [ValidateSet('', 'minimal', 'low', 'medium', 'high', 'xhigh', 'max', 'ultra')][string]$Effort = '',
    [string]$Model = ''
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$result = @{status='unsupported'; reason='no_desktop_window'; confirmed_effort=$null; confirmed_model=$null}

function Get-Controls($root) {
    $condition = New-Object System.Windows.Automation.OrCondition(
        (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button)),
        (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::ComboBox)),
        (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::RadioButton)),
        (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::MenuItem)),
        (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::ListItem))
    )
    $items = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $condition)
    $controls = @()
    for ($i=0; $i -lt [Math]::Min($items.Count, 1000); $i++) {
        if (-not $items[$i].Current.IsOffscreen -and $items[$i].Current.IsEnabled) {$controls += $items[$i]}
    }
    return $controls
}

function Read-Effort($selector) {
    $name = $selector.Current.Name
    if ($name -match '(?i)\b(minimal|low|medium|high|xhigh|max|ultra)\b') {return $Matches[1].ToLowerInvariant()}
    return $null
}

try {
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    $windows = @(Get-Process -Name Codex,ChatGPT -ErrorAction SilentlyContinue | Where-Object {$_.MainWindowHandle -ne 0})
    if ($windows.Count -ne 1) {
        if ($windows.Count -gt 1) {$result.reason='ambiguous_desktop_windows'}
    }
    else {
        $root = [System.Windows.Automation.AutomationElement]::FromHandle($windows[0].MainWindowHandle)
        $controls = @(Get-Controls $root)
        $selectorPattern = '(?i)reasoning effort|thinking level|\u601d\u8003\u7b49\u7ea7|\u601d\u8003\u5f3a\u5ea6|\u63a8\u7406\u5f3a\u5ea6'
        $selectors = @($controls | Where-Object {$_.Current.Name -match $selectorPattern})
        $normalize = {param($text) ($text -replace '[^a-zA-Z0-9]', '').ToLowerInvariant()}
        $models = @($controls | Where-Object {
            $name=$_.Current.Name
            $Model -and ((& $normalize $name) -eq (& $normalize $Model))
        })
        if ($selectors.Count -ne 1) {$result.reason='reasoning_selector_not_uniquely_exposed'}
        elseif ($models.Count -ne 1) {$result.reason='requested_model_not_uniquely_exposed'}
        else {
            $selector=$selectors[0]
            $result.confirmed_model=$Model
            $observed=Read-Effort $selector
            if ($Mode -eq 'Sync' -and $Effort -and $observed -ne $Effort) {
                # Only use semantic controls. Never click coordinates or send a prompt.
                $pattern=$null
                if (-not $selector.TryGetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$pattern)) {
                    $result.reason='selector_not_expandable'
                }
                else {
                    $pattern.Expand()
                    try {
                    Start-Sleep -Milliseconds 250
                    $options=@(Get-Controls $root | Where-Object {
                        $_.Current.Name.Trim().ToLowerInvariant() -eq $Effort -and
                        $_.Current.ControlType -in @([System.Windows.Automation.ControlType]::RadioButton, [System.Windows.Automation.ControlType]::MenuItem, [System.Windows.Automation.ControlType]::ListItem)
                    })
                    if ($options.Count -ne 1) {$result.reason='effort_option_not_uniquely_exposed'}
                    else {
                        $selection=$null
                        if ($options[0].TryGetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern, [ref]$selection)) {$selection.Select()}
                        elseif ($options[0].TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$selection)) {$selection.Invoke()}
                        else {$result.reason='effort_option_not_selectable'}
                        Start-Sleep -Milliseconds 250
                    }
                    }
                    finally {$pattern.Collapse()}
                    $updated=@(Get-Controls $root | Where-Object {$_.Current.Name -match $selectorPattern})
                    $observed=if($updated.Count -eq 1){Read-Effort $updated[0]}else{$null}
                    $modelStillPresent=@(Get-Controls $root | Where-Object {(& $normalize $_.Current.Name) -eq (& $normalize $Model)})
                    if($modelStillPresent.Count -ne 1){$observed=$null; $result.reason='model_changed_during_selection'}
                }
            }
            if ($observed) {
                $result.confirmed_effort=$observed
                $result.status='observed'
                if ($observed -eq $Effort) {$result.status='confirmed'; $result.reason='picker_readback_matches'}
                else {$result.reason='picker_readback_differs'}
            }
        }
    }
}
catch {
    # Exceptions can contain unrelated GUI text. Do not dump them into logs.
    $result.status='unsupported'
    $result.reason='uia_probe_failed'
    $result.confirmed_effort=$null
    $result.confirmed_model=$null
}
$result | ConvertTo-Json -Compress
