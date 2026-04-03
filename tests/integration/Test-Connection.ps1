<#
.SYNOPSIS
    AirBeam connection integration test — remote-controls the running app via
    Win32 PostMessage and validates results via the log file.

.DESCRIPTION
    1. Kills any running AirBeam (waits 3 s for speaker to release session)
    2. Starts AirBeam fresh, records log offset at start
    3. Waits for mDNS discovery
    4. Sends connect command to named speaker
    5. Waits for WM_STREAM_STARTED (using pre-start offset, covers auto-connect too)
    6. Verifies stream stays up for 5 s and re-click is a no-op
    7. Reports PASS / FAIL

.PARAMETER SpeakerName
    Substring of the speaker's display name to connect to.
    If omitted, connects to the first speaker found alphabetically (index 0).

.PARAMETER AirBeamExe
    Path to AirBeam.exe. Defaults to the debug build location.

.PARAMETER DiscoveryTimeoutSec
    Seconds to wait for at least one speaker to be discovered. Default: 30.

.PARAMETER ConnectTimeoutSec
    Seconds to wait for WM_STREAM_STARTED after connect command. Default: 20.
#>
param(
    [string] $SpeakerName         = "",
    [string] $AirBeamExe          = "$PSScriptRoot\..\..\build\msvc-x64-debug\AirBeam.exe",
    [int]    $DiscoveryTimeoutSec = 30,
    [int]    $ConnectTimeoutSec   = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Win32 P/Invoke helpers
# ---------------------------------------------------------------------------
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class AirBeamControl {
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "FindWindowW")]
    public static extern IntPtr FindWindow(string lpClassName, IntPtr lpWindowName);

    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
}
"@

$WM_COMMAND      = 0x0111u
$IDM_DEVICE_BASE = 2000

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
function Find-AirBeamWindow {
    return [AirBeamControl]::FindWindow("AirBeamTrayWnd", [IntPtr]::Zero)
}

function Get-LogPath {
    $today = (Get-Date).ToString("yyyyMMdd")
    return "$env:APPDATA\AirBeam\logs\airbeam-$today.log"
}

function Read-LogShared {
    $logPath = Get-LogPath
    if (-not (Test-Path $logPath)) { return "" }
    try {
        $fs = [System.IO.File]::Open($logPath,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite)
        $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)
        $content = $sr.ReadToEnd()
        $sr.Close(); $fs.Close()
        return $content
    } catch { return "" }
}

function Wait-LogPattern {
    param([string]$Pattern, [int]$TimeoutSec, [string]$Description, [int]$AfterOffset = 0)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    Write-Host "  Waiting for: $Description ($TimeoutSec s)..."
    while ((Get-Date) -lt $deadline) {
        $content = Read-LogShared
        $window = if ($AfterOffset -gt 0 -and $content.Length -gt $AfterOffset) {
            $content.Substring($AfterOffset)
        } else { $content }
        if ($window -match $Pattern) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Get-DiscoveredSpeakers {
    param([int]$AfterOffset = 0)
    $content = Read-LogShared
    $window = if ($AfterOffset -gt 0 -and $content.Length -gt $AfterOffset) {
        $content.Substring($AfterOffset)
    } else { $content }
    $speakers = @()
    foreach ($line in ($window -split "`n")) {
        if ($line -match 'mDNS: discovered "([^"]+)"') {
            $instanceName = $Matches[1]
            $displayName = if ($instanceName -match '@(.+)$') { $Matches[1].Trim() } else { $instanceName.Trim() }
            if ($speakers -notcontains $displayName) {
                $speakers += $displayName
            }
        }
    }
    return ($speakers | Sort-Object)
}

# ---------------------------------------------------------------------------
$pass = $true
$errors = @()

Write-Host ""
Write-Host "=== AirBeam Connection Test ===" -ForegroundColor Cyan

# 1. Kill any existing AirBeam (clean state; wait 3 s for speaker to release session)
Write-Host "`n[1] Ensuring clean start..."
$existing = Get-Process AirBeam -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host "  Stopping existing AirBeam (PID $($existing.Id))..."
    Stop-Process -Id $existing.Id -Force
    Write-Host "  Waiting 3 s for speaker to release previous session..."
    Start-Sleep -Seconds 3
} else {
    Write-Host "  AirBeam not running — no cleanup needed."
}

# 2. Start AirBeam fresh (Logger opens in "w" mode — wipes the log on start)
Write-Host "`n[2] Starting AirBeam..."
$AirBeamExe = Resolve-Path $AirBeamExe
Start-Process $AirBeamExe
Start-Sleep -Seconds 2

# Record log offset AFTER start: the log is always wiped at startup, so offset = 0
$logOffset = 0
Write-Host "  Log offset: $logOffset chars (log wiped at app start)"
$hwnd = Find-AirBeamWindow
if ($hwnd -eq [IntPtr]::Zero) {
    Write-Error "Could not find AirBeam window after starting."
    exit 1
}
Write-Host "  AirBeam window: 0x$($hwnd.ToString('X8'))" -ForegroundColor Green

# 4. Wait for speaker discovery
Write-Host "`n[3] Waiting for speaker discovery..."
$discovered = Wait-LogPattern -Pattern 'mDNS: discovered' -TimeoutSec $DiscoveryTimeoutSec `
                               -Description "speaker discovery" -AfterOffset $logOffset
if (-not $discovered) {
    $errors += "TIMEOUT: No speakers discovered within ${DiscoveryTimeoutSec}s"
    $pass = $false
}

$speakers = Get-DiscoveredSpeakers -AfterOffset $logOffset
Write-Host "  Discovered ($($speakers.Count)): $($speakers -join ', ')" `
    -ForegroundColor $(if ($speakers.Count -gt 0) { 'Green' } else { 'Red' })

# 5. Pick target speaker
$targetIdx = 0
if ($SpeakerName -ne "") {
    $idx = 0
    $found = $false
    foreach ($s in $speakers) {
        if ($s -match [regex]::Escape($SpeakerName)) {
            $targetIdx = $idx
            $found = $true
            Write-Host "  Target: '$s' (index $targetIdx)" -ForegroundColor Green
            break
        }
        $idx++
    }
    if (-not $found) {
        $errors += "Speaker '$SpeakerName' not found in discovered list: $($speakers -join ', ')"
        $pass = $false
    }
} else {
    Write-Host "  Target: index 0 = '$($speakers[0])'" -ForegroundColor Green
}

if ($pass) {
    # 6. Send connect command
    #    Note: we use $logOffset (not current position) for WM_STREAM_STARTED detection.
    #    This means we also catch auto-connect events that happened before this step.
    Write-Host "`n[4] Sending connect command (IDM_DEVICE_BASE + $targetIdx = $($IDM_DEVICE_BASE + $targetIdx))..."
    $cmdId = [IntPtr]($IDM_DEVICE_BASE + $targetIdx)
    [AirBeamControl]::PostMessage($hwnd, $WM_COMMAND, $cmdId, [IntPtr]::Zero) | Out-Null

    # 7. Wait for WM_STREAM_STARTED anywhere after app start (covers auto-connect + manual connect)
    Write-Host "`n[5] Waiting for WM_STREAM_STARTED..."
    $streaming = Wait-LogPattern -Pattern 'WM_STREAM_STARTED' -TimeoutSec $ConnectTimeoutSec `
                                 -Description "streaming start" -AfterOffset $logOffset
    if ($streaming) {
        Write-Host "  STREAMING STARTED" -ForegroundColor Green
    } else {
        $errors += "TIMEOUT: WM_STREAM_STARTED not seen within ${ConnectTimeoutSec}s"
        $pass = $false
        # Show recent RAOP-related log for diagnosis
        $relevant = (Read-LogShared).Substring($logOffset) -split "`n" |
            Select-String -Pattern "RAOP|failed|error|FAIL|406|403|STREAM" -CaseSensitive:$false |
            Select-Object -Last 15
        if ($relevant) {
            Write-Host "  Recent relevant log lines:" -ForegroundColor Yellow
            $relevant | ForEach-Object { Write-Host "    $_" -ForegroundColor Yellow }
        }
    }

    # 8. Verify stream stays up for 5 seconds
    if ($streaming) {
        Write-Host "`n[6] Streaming for 5 seconds..."
        $stableOffset = (Read-LogShared).Length
        Start-Sleep -Seconds 5

        $droppedDuringWait = (Read-LogShared).Substring($stableOffset) -match 'WM_STREAM_STOPPED'
        if ($droppedDuringWait) {
            $errors += "FAIL: Stream dropped within 5 seconds (RAOP session unstable)"
            $pass = $false
        } else {
            Write-Host "  Stream stable for 5 s" -ForegroundColor Green

            # 9. Re-click same speaker — should be a no-op
            Write-Host "  Re-clicking same speaker (should be no-op)..."
            $noop_offset = (Read-LogShared).Length
            [AirBeamControl]::PostMessage($hwnd, $WM_COMMAND, $cmdId, [IntPtr]::Zero) | Out-Null
            Start-Sleep -Seconds 2
            $noopLog = Read-LogShared
            $noopWindow = if ($noopLog.Length -gt $noop_offset) { $noopLog.Substring($noop_offset) } else { $noopLog }
            $stopped = $noopWindow -match 'WM_STREAM_STOPPED'
            if ($stopped) {
                $errors += "FAIL: Stream stopped after re-click (same speaker should be no-op)"
                $pass = $false
            } else {
                Write-Host "  No-op confirmed — still streaming" -ForegroundColor Green
            }
        }
    }
}

# Diagnostics: recent log tail
Write-Host "`n[7] Recent log (new entries only):" -ForegroundColor Cyan
$finalLog = Read-LogShared
$recentLog = if ($finalLog.Length -gt $logOffset) { $finalLog.Substring($logOffset) } else { $finalLog }
$recentLog -split "`n" | Select-Object -Last 25 | ForEach-Object { Write-Host "  $_" }

# Result
Write-Host ""
if ($pass) {
    Write-Host "=== PASS ===" -ForegroundColor Green
} else {
    Write-Host "=== FAIL ===" -ForegroundColor Red
    $errors | ForEach-Object { Write-Host "  ERROR: $_" -ForegroundColor Red }
    exit 1
}
