#!/usr/bin/env pwsh
# fetch-bonjour.ps1
#
# Downloads Apple Bonjour Print Services (BonjourPSSetup.exe) from the pinned Apple CDN URL,
# verifies the SHA-256 checksum, and places the verified binary at $OutputDir\BonjourPSSetup.exe.
#
# Used as a CMake custom command pre-build step. Exit code 1 on any failure so CMake
# propagates FATAL_ERROR (see CMakeLists.txt fetch-bonjour target).
#
# Usage:
#   pwsh installer/deps/fetch-bonjour.ps1 -OutputDir installer/deps
#
# First-time hash pinning:
#   Run with -ComputeHashOnly to download and print the SHA-256 without verifying.
#   Copy the printed hash into $ExpectedSha256 below.

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDir,
    [switch]$ComputeHashOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ── Pinned release ────────────────────────────────────────────────────────────
$BonjourVersion  = '3.0.0.10'
$Url             = 'https://support.apple.com/downloads/DL999/en_US/BonjourPSSetup.exe'
# SHA-256 of the pinned BonjourPSSetup.exe (set after first download — see T005).
# Run: pwsh fetch-bonjour.ps1 -OutputDir <dir> -ComputeHashOnly
$ExpectedSha256  = '1004D3BCE4EE673D4BAB38E593EC49EEE50436B86AA5BCF567F29BC24B9CFDE8'   # Bonjour Print Services 3.0.0.10 — verified 2026-03-26
# ─────────────────────────────────────────────────────────────────────────────

$OutFile = Join-Path $OutputDir 'BonjourPSSetup.exe'

function Get-FileSha256 ([string]$Path) {
    (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToUpper()
}

# ── Skip download if already verified ────────────────────────────────────────
if (-not $ComputeHashOnly -and (Test-Path $OutFile) -and $ExpectedSha256 -ne 'UNSET') {
    $existing = Get-FileSha256 $OutFile
    if ($existing -eq $ExpectedSha256.ToUpper()) {
        Write-Host "fetch-bonjour: $OutFile already present and hash-verified — skipping download."
        exit 0
    }
    Write-Warning "fetch-bonjour: Existing file hash mismatch (expected $ExpectedSha256, got $existing). Re-downloading."
    Remove-Item $OutFile -Force
}

# ── Ensure output directory exists ───────────────────────────────────────────
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

# ── Download ──────────────────────────────────────────────────────────────────
Write-Host "fetch-bonjour: Downloading Bonjour Print Services $BonjourVersion from $Url ..."
try {
    Invoke-WebRequest -Uri $Url -OutFile $OutFile -UseBasicParsing
} catch {
    Write-Error "fetch-bonjour: FATAL: Failed to download from $Url`n  Error: $_`n  Verify internet connectivity or update the pinned URL in fetch-bonjour.ps1."
    exit 1
}

if (-not (Test-Path $OutFile)) {
    Write-Error "fetch-bonjour: FATAL: Download appeared to succeed but $OutFile does not exist."
    exit 1
}

# ── Hash verification ─────────────────────────────────────────────────────────
$actual = Get-FileSha256 $OutFile
Write-Host "fetch-bonjour: SHA-256 = $actual"

if ($ComputeHashOnly) {
    Write-Host ""
    Write-Host "fetch-bonjour: -ComputeHashOnly mode. Copy the hash above into `$ExpectedSha256 in fetch-bonjour.ps1."
    exit 0
}

if ($ExpectedSha256 -eq 'UNSET') {
    Write-Error "fetch-bonjour: FATAL: `$ExpectedSha256 is not set. Run with -ComputeHashOnly first, then pin the hash."
    exit 1
}

if ($actual -ne $ExpectedSha256.ToUpper()) {
    Remove-Item $OutFile -Force
    Write-Error @"
fetch-bonjour: FATAL: SHA-256 mismatch for BonjourPSSetup.exe.
  Expected : $ExpectedSha256
  Actual   : $actual
  URL      : $Url
  The Apple CDN may have updated the file. Re-run with -ComputeHashOnly to get the new hash,
  verify it is the expected Bonjour version $BonjourVersion, then update `$ExpectedSha256.
"@
    exit 1
}

Write-Host "fetch-bonjour: Hash verified OK. $OutFile is ready."
exit 0
