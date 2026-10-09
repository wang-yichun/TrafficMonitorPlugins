param(
    [string]$HostDirectory = (Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))) 'TrafficMonitor'),
    [switch]$ValidateOnly,
    [switch]$SkipStart
)

$ErrorActionPreference = 'Stop'

try {
    $HostDirectory = [IO.Path]::GetFullPath($HostDirectory)
    $hostExe = Join-Path $HostDirectory 'TrafficMonitor.exe'
    $configPath = Join-Path $HostDirectory 'config.ini'
    $pluginDll = Join-Path $HostDirectory 'plugins\iKuuuTraffic.dll'
    $buildDll = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'bin\x64\Release\iKuuuTraffic.dll'

    foreach ($requiredFile in @($hostExe, $configPath, $buildDll)) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
            throw "Required file is missing: $requiredFile"
        }
    }

    $sourceHash = (Get-FileHash -LiteralPath $buildDll -Algorithm SHA256).Hash
    $configBytes = [IO.File]::ReadAllBytes($configPath)
    $hasUtf8Bom = $configBytes.Length -ge 3 -and
        $configBytes[0] -eq 0xEF -and $configBytes[1] -eq 0xBB -and $configBytes[2] -eq 0xBF
    $utf8 = New-Object System.Text.UTF8Encoding($false, $true)
    $configText = $utf8.GetString($configBytes, $(if ($hasUtf8Bom) { 3 } else { 0 }), $configBytes.Length - $(if ($hasUtf8Bom) { 3 } else { 0 }))
    $newline = if ($configText.Contains("`r`n")) { "`r`n" } elseif ($configText.Contains("`n")) { "`n" } else { "`r" }
    $configLines = [System.Collections.Generic.List[string]]::new()
    foreach ($line in [regex]::Split($configText, "`r`n|`n|`r")) { $configLines.Add($line) }

    function Find-IniKeyLine {
        param([string[]]$Lines, [string]$SectionName, [string]$KeyName)

        $sectionMatches = @()
        for ($i = 0; $i -lt $Lines.Count; $i++) {
            if ($Lines[$i] -match '^\s*\[([^\]]+)\]\s*$' -and $Matches[1] -ieq $SectionName) {
                $sectionMatches += $i
            }
        }
        if ($sectionMatches.Count -ne 1) { throw "Expected exactly one [$SectionName] section in config.ini." }

        $start = $sectionMatches[0] + 1
        $end = $Lines.Count
        for ($i = $start; $i -lt $Lines.Count; $i++) {
            if ($Lines[$i] -match '^\s*\[') { $end = $i; break }
        }
        $keyMatches = @()
        for ($i = $start; $i -lt $end; $i++) {
            if ($Lines[$i] -match ('^\s*' + [regex]::Escape($KeyName) + '\s*=')) { $keyMatches += $i }
        }
        if ($keyMatches.Count -ne 1) { throw "Expected exactly one '$KeyName' key in [$SectionName]." }
        return $keyMatches[0]
    }

    $displayLine = Find-IniKeyLine -Lines $configLines.ToArray() -SectionName 'task_bar' -KeyName 'plugin_display_item'
    $disabledLine = Find-IniKeyLine -Lines $configLines.ToArray() -SectionName 'config' -KeyName 'plugin_disabled'

    $displayMatch = [regex]::Match($configLines[$displayLine], '^(?<prefix>\s*plugin_display_item\s*=\s*)(?<value>.*)$', 'IgnoreCase')
    $disabledMatch = [regex]::Match($configLines[$disabledLine], '^(?<prefix>\s*plugin_disabled\s*=\s*)(?<value>.*)$', 'IgnoreCase')
    if (-not $displayMatch.Success -or -not $disabledMatch.Success) { throw 'Could not parse the plugin settings in config.ini.' }

    $displayValue = $displayMatch.Groups['value'].Value
    $displayItems = @($displayValue -split ',')
    $missingDisplayItems = @()
    foreach ($itemId in @('iKuuuQuotaGB1', 'iKuuuTodayGB1')) {
        if (-not ($displayItems | Where-Object { $_.Trim() -ceq $itemId })) { $missingDisplayItems += $itemId }
    }
    $newDisplayValue = $displayValue
    if ($missingDisplayItems.Count -gt 0) {
        if ([string]::IsNullOrEmpty($newDisplayValue)) { $newDisplayValue = $missingDisplayItems -join ',' }
        elseif ($newDisplayValue.EndsWith(',')) { $newDisplayValue += $missingDisplayItems -join ',' }
        else { $newDisplayValue += ',' + ($missingDisplayItems -join ',') }
    }

    $disabledValue = $disabledMatch.Groups['value'].Value
    $disabledItems = @($disabledValue -split ',')
    $newDisabledValue = $disabledValue
    if ($disabledItems | Where-Object { $_.Trim() -ieq 'iKuuuTraffic.dll' }) {
        $newDisabledValue = @($disabledItems | Where-Object { $_.Trim() -ine 'iKuuuTraffic.dll' }) -join ','
    }

    $configLines[$displayLine] = $displayMatch.Groups['prefix'].Value + $newDisplayValue
    $configLines[$disabledLine] = $disabledMatch.Groups['prefix'].Value + $newDisabledValue
    $updatedConfigText = [string]::Join($newline, $configLines)

    if ($ValidateOnly) {
        Write-Host "Host:       $hostExe"
        Write-Host "Config:     $configPath"
        Write-Host "Plugin:     $pluginDll"
        Write-Host "Build DLL:  $buildDll"
        Write-Host "SHA256:     $sourceHash"
        Write-Host "Display IDs: $newDisplayValue"
        Write-Host 'Validation passed; no process or file was changed.'
        exit 0
    }

    if ($null -eq ('TrafficMonitorInstallWindow' -as [type])) {
        Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class TrafficMonitorInstallWindow {
    public delegate bool EnumWindowsProc(IntPtr window, IntPtr lParam);
    private static IntPtr matchedWindow;
    public static int MatchCount;

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern int GetClassName(IntPtr window, System.Text.StringBuilder className, int maxCount);
    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    public static IntPtr FindTargetWindow(uint targetProcessId) {
        matchedWindow = IntPtr.Zero;
        MatchCount = 0;
        EnumWindowsProc callback = delegate(IntPtr window, IntPtr lParam) {
            uint processId = 0;
            GetWindowThreadProcessId(window, out processId);
            if (processId == targetProcessId) {
                System.Text.StringBuilder className = new System.Text.StringBuilder(256);
                if (GetClassName(window, className, className.Capacity) > 0 &&
                    className.ToString() == "TrafficMonitor_r7XZaS4p") {
                    MatchCount++;
                    matchedWindow = window;
                }
            }
            return true;
        };
        EnumWindows(callback, IntPtr.Zero);
        return MatchCount == 1 ? matchedWindow : IntPtr.Zero;
    }
}
'@
    }

    $targetProcesses = @(Get-CimInstance Win32_Process -Filter "Name='TrafficMonitor.exe'" | Where-Object {
        $_.ExecutablePath -and [IO.Path]::GetFullPath($_.ExecutablePath) -ieq $hostExe
    })
    if ($targetProcesses.Count -gt 1) { throw "More than one TrafficMonitor process is running from $HostDirectory." }
    if ($targetProcesses.Count -eq 1) {
        [uint32]$windowProcessId = 0
        $targetProcessId = [uint32]$targetProcesses[0].ProcessId
        $window = [TrafficMonitorInstallWindow]::FindTargetWindow($targetProcessId)
        if ([TrafficMonitorInstallWindow]::MatchCount -eq 0) {
            throw 'Could not find a top-level window with class TrafficMonitor_r7XZaS4p for the target process.'
        }
        if ([TrafficMonitorInstallWindow]::MatchCount -ne 1) {
            throw 'The target process has multiple TrafficMonitor_r7XZaS4p windows; no process was changed.'
        }
        if ($window -eq [IntPtr]::Zero) { throw 'Could not resolve the unique target TrafficMonitor window; no process was changed.' }
        [void][TrafficMonitorInstallWindow]::GetWindowThreadProcessId($window, [ref]$windowProcessId)
        if ($windowProcessId -ne $targetProcessId) {
            throw 'The TrafficMonitor window belongs to a different process; no process was changed.'
        }
        $running = Get-Process -Id $windowProcessId -ErrorAction SilentlyContinue
        if ($running) {
            Write-Host "Closing TrafficMonitor (PID $windowProcessId) and allowing it to save settings..."
            if (-not [TrafficMonitorInstallWindow]::PostMessage($window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)) {
                throw 'Could not request TrafficMonitor to exit.'
            }
            if (-not $running.WaitForExit(15000)) {
                throw 'TrafficMonitor did not exit within 15 seconds. Close any open dialogs and try again.'
            }
        }
    }

    # TrafficMonitor saves its live settings while exiting, so merge into that final config.
    $configBytes = [IO.File]::ReadAllBytes($configPath)
    $hasUtf8Bom = $configBytes.Length -ge 3 -and
        $configBytes[0] -eq 0xEF -and $configBytes[1] -eq 0xBB -and $configBytes[2] -eq 0xBF
    $configText = $utf8.GetString($configBytes, $(if ($hasUtf8Bom) { 3 } else { 0 }), $configBytes.Length - $(if ($hasUtf8Bom) { 3 } else { 0 }))
    $newline = if ($configText.Contains("`r`n")) { "`r`n" } elseif ($configText.Contains("`n")) { "`n" } else { "`r" }
    $configLines = [System.Collections.Generic.List[string]]::new()
    foreach ($line in [regex]::Split($configText, "`r`n|`n|`r")) { $configLines.Add($line) }
    $displayLine = Find-IniKeyLine -Lines $configLines.ToArray() -SectionName 'task_bar' -KeyName 'plugin_display_item'
    $disabledLine = Find-IniKeyLine -Lines $configLines.ToArray() -SectionName 'config' -KeyName 'plugin_disabled'
    $displayMatch = [regex]::Match($configLines[$displayLine], '^(?<prefix>\s*plugin_display_item\s*=\s*)(?<value>.*)$', 'IgnoreCase')
    $disabledMatch = [regex]::Match($configLines[$disabledLine], '^(?<prefix>\s*plugin_disabled\s*=\s*)(?<value>.*)$', 'IgnoreCase')
    if (-not $displayMatch.Success -or -not $disabledMatch.Success) { throw 'Could not parse the saved plugin settings in config.ini.' }
    $displayValue = $displayMatch.Groups['value'].Value
    $displayItems = @($displayValue -split ',')
    $missingDisplayItems = @()
    foreach ($itemId in @('iKuuuQuotaGB1', 'iKuuuTodayGB1')) {
        if (-not ($displayItems | Where-Object { $_.Trim() -ceq $itemId })) { $missingDisplayItems += $itemId }
    }
    $newDisplayValue = $displayValue
    if ($missingDisplayItems.Count -gt 0) {
        if ([string]::IsNullOrEmpty($newDisplayValue)) { $newDisplayValue = $missingDisplayItems -join ',' }
        elseif ($newDisplayValue.EndsWith(',')) { $newDisplayValue += $missingDisplayItems -join ',' }
        else { $newDisplayValue += ',' + ($missingDisplayItems -join ',') }
    }
    $disabledValue = $disabledMatch.Groups['value'].Value
    $disabledItems = @($disabledValue -split ',')
    $newDisabledValue = $disabledValue
    if ($disabledItems | Where-Object { $_.Trim() -ieq 'iKuuuTraffic.dll' }) {
        $newDisabledValue = @($disabledItems | Where-Object { $_.Trim() -ine 'iKuuuTraffic.dll' }) -join ','
    }
    $configLines[$displayLine] = $displayMatch.Groups['prefix'].Value + $newDisplayValue
    $configLines[$disabledLine] = $disabledMatch.Groups['prefix'].Value + $newDisabledValue
    $updatedConfigText = [string]::Join($newline, $configLines)

    $backupRoot = Join-Path $env:LOCALAPPDATA 'TrafficMonitor-plugin-backups'
    New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
    $backupName = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $backupDirectory = Join-Path $backupRoot $backupName
    $suffix = 1
    while (Test-Path -LiteralPath $backupDirectory) {
        $backupDirectory = Join-Path $backupRoot ($backupName + '-' + $suffix)
        $suffix++
    }
    New-Item -ItemType Directory -Path $backupDirectory | Out-Null
    Copy-Item -LiteralPath $configPath -Destination (Join-Path $backupDirectory 'config.ini')
    if (Test-Path -LiteralPath $pluginDll -PathType Leaf) {
        Copy-Item -LiteralPath $pluginDll -Destination (Join-Path $backupDirectory 'iKuuuTraffic.dll')
    }
    Write-Host "Saved pre-install files in $backupDirectory"

    [IO.Directory]::CreateDirectory((Split-Path -Parent $pluginDll)) | Out-Null
    $updatedConfigBytes = $utf8.GetBytes($updatedConfigText)
    if ($hasUtf8Bom) {
        $preamble = [byte[]](0xEF, 0xBB, 0xBF)
        $updatedConfigBytes = $preamble + $updatedConfigBytes
    }
    [IO.File]::WriteAllBytes($configPath, $updatedConfigBytes)
    Copy-Item -LiteralPath $buildDll -Destination $pluginDll -Force

    $installedHash = (Get-FileHash -LiteralPath $pluginDll -Algorithm SHA256).Hash
    if ($installedHash -ne $sourceHash) { throw "Installed DLL SHA256 does not match the build. Backup: $backupDirectory" }
    Write-Host "Installed iKuuuTraffic.dll (SHA256 $installedHash)."
    Write-Host "Enabled plugin display IDs: $newDisplayValue"

    if (-not $SkipStart) {
        $started = Start-Process -FilePath $hostExe -WorkingDirectory $HostDirectory -WindowStyle Hidden -PassThru
        Start-Sleep -Seconds 2
        $started.Refresh()
        if ($started.HasExited) { throw 'The new TrafficMonitor process exited immediately.' }
        Write-Host "Started $hostExe (PID $($started.Id))."
    } else {
        Write-Host 'TrafficMonitor was not started because -SkipStart was specified.'
    }
    exit 0
}
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
