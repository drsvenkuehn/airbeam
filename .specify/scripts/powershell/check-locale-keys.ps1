<#
.SYNOPSIS
    Verifies that every IDS_* key defined in the canonical English RC file is
    present in every other locale RC file under LocaleDir.

.PARAMETER CanonicalFile
    Path to the canonical English locale file (strings_en.rc).

.PARAMETER LocaleDir
    Path to the directory containing all locale RC files (resources/locales/).

.EXAMPLE
    .\check-locale-keys.ps1 `
        -CanonicalFile  "resources/locales/strings_en.rc" `
        -LocaleDir      "resources/locales"
#>

param(
    [Parameter(Mandatory = $true)]
    [string]$CanonicalFile,

    [Parameter(Mandatory = $true)]
    [string]$LocaleDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ── Helper: extract all IDS_* identifiers from an RC file ──────────────────
function Get-IdsKeys {
    param([string]$FilePath)

    $keys = [System.Collections.Generic.SortedSet[string]]::new()

    # Match lines of the form:   IDS_SOME_KEY   "string..."
    # The key must start at the beginning of an unindented or indented line.
    $pattern = '^\s+(IDS_[A-Z0-9_]+)\s+'

    Get-Content -LiteralPath $FilePath -Encoding UTF8 | ForEach-Object {
        if ($_ -match $pattern) {
            [void]$keys.Add($Matches[1])
        }
    }

    return $keys
}

# ── Resolve paths ────────────────────────────────────────────────────────────
$CanonicalFile = Resolve-Path -LiteralPath $CanonicalFile | Select-Object -ExpandProperty Path
$LocaleDir     = Resolve-Path -LiteralPath $LocaleDir     | Select-Object -ExpandProperty Path

if (-not (Test-Path -LiteralPath $CanonicalFile)) {
    Write-Error "Canonical file not found: $CanonicalFile"
    exit 1
}

# ── Load canonical keys ───────────────────────────────────────────────────────
$canonicalKeys = Get-IdsKeys -FilePath $CanonicalFile
if ($canonicalKeys.Count -eq 0) {
    Write-Error "No IDS_* keys found in canonical file: $CanonicalFile"
    exit 1
}

Write-Host "Canonical file : $CanonicalFile"
Write-Host "Keys defined   : $($canonicalKeys.Count)"
Write-Host ""

# ── Check each non-canonical RC file ─────────────────────────────────────────
$otherFiles = Get-ChildItem -LiteralPath $LocaleDir -Filter '*.rc' |
              Where-Object { $_.FullName -ne $CanonicalFile }

if ($otherFiles.Count -eq 0) {
    Write-Host "No non-canonical RC files found in $LocaleDir — nothing to check."
    exit 0
}

$overallOk  = $true
$allMissing = [System.Collections.Generic.List[string]]::new()

foreach ($file in $otherFiles | Sort-Object Name) {
    $fileKeys    = Get-IdsKeys -FilePath $file.FullName
    $missingKeys = $canonicalKeys | Where-Object { -not $fileKeys.Contains($_) }

    if ($missingKeys) {
        $overallOk = $false
        Write-Host "[FAIL] $($file.Name)" -ForegroundColor Red
        foreach ($key in $missingKeys) {
            $msg = "       Missing key: $key"
            Write-Host $msg -ForegroundColor Yellow
            $allMissing.Add("$($file.Name): $key")
        }
    }
    else {
        Write-Host "[ OK ] $($file.Name)" -ForegroundColor Green
    }
}

Write-Host ""

if (-not $overallOk) {
    Write-Host "LOCALE KEY CHECK FAILED — $($allMissing.Count) missing key(s):" -ForegroundColor Red
    $allMissing | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
    exit 1
}

Write-Host "All locale files have complete IDS_* key coverage." -ForegroundColor Green
exit 0
