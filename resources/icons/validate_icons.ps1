# validate_icons.ps1
# Validates all 11 AirBeam tray ICO files meet the minimum contract:
#   - File exists
#   - File is a valid ICO (correct magic bytes)
#   - File contains >= 2 frames (16x16 and 32x32 minimum)
#   - File is larger than a placeholder (~680 bytes)
#
# Usage: pwsh resources/icons/validate_icons.ps1
# Exit:  0 on full pass, 1 on any failure

$icons = @(
    "airbeam_idle.ico",
    "airbeam_streaming.ico",
    "airbeam_error.ico",
    "airbeam_connecting_001.ico",
    "airbeam_connecting_002.ico",
    "airbeam_connecting_003.ico",
    "airbeam_connecting_004.ico",
    "airbeam_connecting_005.ico",
    "airbeam_connecting_006.ico",
    "airbeam_connecting_007.ico",
    "airbeam_connecting_008.ico"
)

$MIN_BYTES  = 1024   # > 1 KB rules out any single-frame placeholder
$MIN_FRAMES = 2
$pass = $true

function Get-IcoFrameCount {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 6) { return 0 }
    if ($bytes[0] -ne 0 -or $bytes[1] -ne 0) { return -1 }  # bad magic
    if ($bytes[2] -ne 1 -or $bytes[3] -ne 0) { return -1 }  # not ICO type
    return [int]$bytes[4] + ([int]$bytes[5] * 256)
}

$scriptDir = Split-Path $MyInvocation.MyCommand.Path -Parent

Write-Host ""
Write-Host "AirBeam Icon Validation"
Write-Host ("=" * 62)
Write-Host ("{0,-44} {1,6}  {2,6}  {3}" -f "File", "Size", "Frames", "Status")
Write-Host ("-" * 62)

foreach ($ico in $icons) {
    $path = Join-Path $scriptDir $ico

    if (-not (Test-Path $path)) {
        Write-Host ("{0,-44} {1,6}  {2,6}  FAIL (missing)" -f $ico, "-", "-") -ForegroundColor Red
        $pass = $false
        continue
    }

    $size   = (Get-Item $path).Length
    $frames = Get-IcoFrameCount $path
    $sizeKb = [Math]::Round($size / 1024, 1)

    $ok = ($frames -ge $MIN_FRAMES) -and ($size -ge $MIN_BYTES)
    $statusText = if ($ok) { "OK" } else {
        if ($frames -lt 0) { "FAIL (bad magic)" }
        elseif ($frames -lt $MIN_FRAMES) { "FAIL (<$MIN_FRAMES frames)" }
        else { "FAIL (<1KB placeholder?)" }
    }
    $color = if ($ok) { "Green" } else { "Red" }
    if (-not $ok) { $pass = $false }

    Write-Host ("{0,-44} {1,5}KB  {2,6}  $statusText" -f $ico, $sizeKb, $frames) -ForegroundColor $color
}

Write-Host ("=" * 62)
if ($pass) {
    Write-Host "PASS — all 11 icons valid" -ForegroundColor Green
    exit 0
} else {
    Write-Host "FAIL — one or more icons are invalid or missing" -ForegroundColor Red
    exit 1
}
