<#
.SYNOPSIS
    AirBeam 24-hour stress test monitor (T034 release blocker).

.DESCRIPTION
    Samples AirBeam.exe process memory every IntervalSeconds seconds for
    DurationHours hours and writes results to a CSV file.

    PASS criteria (spec section IV):
      - Working-set linear growth slope < 7 KB/min over the run
      - Total working-set growth < 10 MB over the run
      - AirBeam process survives the full duration
      - shairport-sync Docker container stays running

    SETUP (run these before starting the script):
      1. docker compose up -d            # start shairport-sync receiver
      2. Build AirBeam (Debug or Release) and launch it
      3. In the AirBeam tray menu, connect to "AirBeam-Test"
      4. Ensure Windows is playing audio (loopback capture needs a signal)
      5. Run this script in a separate PowerShell window

.PARAMETER DurationHours
    Test duration in hours. Default: 24.
    Use a small value like 0.05 (3 minutes) to verify the script runs correctly.

.PARAMETER IntervalSeconds
    Sampling interval in seconds. Default: 60.

.PARAMETER ProcessName
    Windows process name (no .exe). Default: AirBeam.

.PARAMETER OutDir
    Directory for output files. Default: tests\stress\results\ next to this script.

.EXAMPLE
    # Full 24-hour run
    .\tests\stress\monitor_stress_test.ps1

    # Quick 3-minute smoke test
    .\tests\stress\monitor_stress_test.ps1 -DurationHours 0.05 -IntervalSeconds 10
#>

param(
    [double] $DurationHours   = 24,
    [int]    $IntervalSeconds = 60,
    [string] $ProcessName     = "AirBeam",
    [string] $OutDir          = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $OutDir) {
    $OutDir = Join-Path $ScriptDir "results"
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$RunId   = Get-Date -Format "yyyyMMdd_HHmmss"
$CsvPath = Join-Path $OutDir "stress_$RunId.csv"
$LogPath = Join-Path $OutDir "stress_$RunId.log"

function Write-Log {
    param([string]$Msg)
    $ts   = Get-Date -Format "HH:mm:ss"
    $line = "[$ts] $Msg"
    Write-Host $line
    Add-Content -Path $LogPath -Value $line
}

# ---------------------------------------------------------------------------
# Pre-flight checks
# ---------------------------------------------------------------------------
Write-Log "=== AirBeam stress-test monitor starting ==="
Write-Log "Duration : $DurationHours h  |  Interval : $IntervalSeconds s"
Write-Log "CSV      : $CsvPath"

$proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue
if (-not $proc) {
    Write-Error "Process '$ProcessName' not found. Start AirBeam and connect to a speaker first."
    exit 1
}
Write-Log "AirBeam PID: $($proc.Id)"

$dockerRunning = $false
try {
    $cstatus = & docker ps --filter "ancestor=mikebrady/shairport-sync" --format "{{.Status}}" 2>$null
    if ($cstatus -match "^Up") { $dockerRunning = $true }
} catch { }

if ($dockerRunning) {
    Write-Log "shairport-sync container: RUNNING"
} else {
    Write-Log "WARNING: shairport-sync container not detected. Run 'docker compose up -d' first."
}

# ---------------------------------------------------------------------------
# CSV header
# ---------------------------------------------------------------------------
"Timestamp,ElapsedMin,WorkingSetMB,PrivateBytesMB,VirtualMB,ContainerUp,ProcessAlive" |
    Out-File -FilePath $CsvPath -Encoding UTF8

# ---------------------------------------------------------------------------
# Sampling loop
# ---------------------------------------------------------------------------
$startTime  = Get-Date
$endTime    = $startTime.AddHours($DurationHours)
$sampleNum  = 0
$wsSamples  = New-Object System.Collections.Generic.List[double]

Write-Log "Sampling started. Press Ctrl+C to abort early."

while ((Get-Date) -lt $endTime) {
    $now     = Get-Date
    $elapsed = ($now - $startTime).TotalMinutes

    $proc  = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue
    $alive = ($null -ne $proc)

    if ($alive) {
        $wsMB   = [math]::Round($proc.WorkingSet64 / 1048576.0, 2)
        $privMB = [math]::Round($proc.PrivateMemorySize64 / 1048576.0, 2)
        $virtMB = [math]::Round($proc.VirtualMemorySize64 / 1048576.0, 2)
    } else {
        $wsMB   = 0
        $privMB = 0
        $virtMB = 0
    }

    $contUp = "0"
    try {
        $cstatus = & docker ps --filter "ancestor=mikebrady/shairport-sync" --format "{{.Status}}" 2>$null
        if ($cstatus -match "^Up") { $contUp = "1" }
    } catch { }

    $aliveInt = if ($alive) { 1 } else { 0 }
    $ts = $now.ToString("yyyy-MM-dd HH:mm:ss")
    "$ts,$([math]::Round($elapsed,2)),$wsMB,$privMB,$virtMB,$contUp,$aliveInt" |
        Add-Content -Path $CsvPath

    if ($alive) { $wsSamples.Add($wsMB) }

    $sampleNum++
    if ($sampleNum % 10 -eq 0) {
        $msg = "Sample {0,5}  elapsed={1,6:F1} min  WS={2,7:F2} MB  Private={3,7:F2} MB  Container={4}" `
               -f $sampleNum, $elapsed, $wsMB, $privMB, $contUp
        Write-Log $msg
    }

    if (-not $alive) {
        Write-Log "CRITICAL: AirBeam exited at sample $sampleNum (elapsed $([math]::Round($elapsed,1)) min)"
        break
    }

    Start-Sleep -Seconds $IntervalSeconds
}

# ---------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------
Write-Log ""
Write-Log "=== Analysis ==="

$testPass = $true
$failures = New-Object System.Collections.Generic.List[string]

# Check 1: Process survived
$proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue
if ($proc) {
    Write-Log "Process survival : PASS"
} else {
    $failures.Add("FAIL: AirBeam process exited during the test")
    $testPass = $false
}

# Check 2: Memory growth via linear regression on working-set samples
if ($wsSamples.Count -ge 2) {
    $n     = $wsSamples.Count
    $xMean = ($n - 1) / 2.0
    $sum   = 0.0
    foreach ($s in $wsSamples) { $sum += $s }
    $yMean = $sum / $n

    $num = 0.0
    $den = 0.0
    for ($i = 0; $i -lt $n; $i++) {
        $dx   = $i - $xMean
        $num += $dx * ($wsSamples[$i] - $yMean)
        $den += $dx * $dx
    }

    if ($den -gt 0) {
        # slope in MB per sample; convert to KB/min
        $slopeMBperSample = $num / $den
        $samplesPerMin    = 60.0 / $IntervalSeconds
        $slopeKBperMin    = $slopeMBperSample * 1024.0 / $samplesPerMin
    } else {
        $slopeKBperMin = 0.0
    }

    $growthMB = $wsSamples[$n - 1] - $wsSamples[0]

    Write-Log ("Memory slope : {0:F2} KB/min  (limit 7 KB/min)" -f $slopeKBperMin)
    Write-Log ("Total growth : {0:F2} MB      (limit 10 MB)"    -f $growthMB)

    if ($slopeKBperMin -gt 7 -or $growthMB -gt 10) {
        $failures.Add("FAIL: Memory growth exceeded limit (slope=$([math]::Round($slopeKBperMin,2)) KB/min, total=$([math]::Round($growthMB,2)) MB)")
        $testPass = $false
    } else {
        Write-Log "Memory growth: PASS"
    }
} else {
    Write-Log "WARNING: Too few samples ($($wsSamples.Count)) for regression"
}

# Check 3: Container survived
try {
    $cstatus = & docker ps --filter "ancestor=mikebrady/shairport-sync" --format "{{.Status}}" 2>$null
    if ($cstatus -match "^Up") {
        Write-Log "Container survival: PASS"
    } else {
        $failures.Add("FAIL: shairport-sync container exited during the test")
        $testPass = $false
    }
} catch {
    Write-Log "WARNING: Could not verify container status"
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
Write-Log ""
Write-Log "============================================================"
if ($testPass) {
    Write-Log "OVERALL RESULT: PASS -- T034 release blocker CLEARED"
} else {
    Write-Log "OVERALL RESULT: FAIL"
    foreach ($f in $failures) { Write-Log "  $f" }
}
Write-Log "Data file: $CsvPath"
Write-Log "============================================================"

$exitCode = if ($testPass) { 0 } else { 1 }
exit $exitCode
