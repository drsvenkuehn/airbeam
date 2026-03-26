# Data Model: Feature 006 — Bundle Bonjour Installer

This feature introduces no persistent runtime data model changes. All entities are
build-time or install-time constructs.

## Entities

### BonjourFetchConfig (build-time constant, `fetch-bonjour.ps1`)

| Field | Type | Value | Notes |
|-------|------|-------|-------|
| `$Url` | `string` | `https://support.apple.com/downloads/DL999/en_US/BonjourPSSetup.exe` | Pinned Apple CDN URL |
| `$ExpectedSha256` | `string` | *determined at first fetch* | SHA-256 hex string (upper-case); must be set before first CI run |
| `$BonjourVersion` | `string` | `3.0.0.10` | Informational; matches the pinned URL |
| `$OutputFile` | `path` | `$OutputDir\BonjourPSSetup.exe` | Destination path (parameter-driven) |

**Invariants**:
- If `$OutputFile` exists and its SHA-256 matches `$ExpectedSha256`, the download is skipped.
- If `$OutputFile` exists but SHA-256 does NOT match, it is deleted before re-download.
- Script exits with code 1 on network failure or hash mismatch (CMake propagates as FATAL_ERROR).

---

### BonjourDetectionResult (installer runtime, NSIS variable)

| Field | Type | Values | Notes |
|-------|------|--------|-------|
| `$0` (registry read result) | NSIS string | non-empty = present, `${Errors}` = absent | Read from `HKLM\SYSTEM\CurrentControlSet\Services\Bonjour Service`, value `ImagePath` |
| `BonjourInstalled` (implicit) | boolean | true if no error, false if error | Drives conditional install block |

**Detection logic**:
```nsis
ClearErrors
ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Services\Bonjour Service" "ImagePath"
${If} ${Errors}
    ; Bonjour absent — install silently
${EndIf}
```

---

### BonjourInstallResult (installer runtime, NSIS variable)

| Field | Type | Values | Notes |
|-------|------|--------|-------|
| `$0` (ExecWait exit code) | integer | 0 = success, non-zero = failure | Populated after `ExecWait` |

**Failure handling**:
```nsis
${If} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP "Bonjour installation failed — AirBeam cannot continue.$\n$\nError code: $0"
    Abort
${EndIf}
```

---

### CombinedLicenseFile (static build artifact, `installer/licenses/combined-license.txt`)

| Section | Content |
|---------|---------|
| Header 1 | `=== AirBeam MIT License ===` |
| Body 1 | AirBeam MIT license text (from `LICENSE`) |
| Separator | blank line |
| Header 2 | `=== Apple Bonjour SDK License ===` |
| Body 2 | Apple Bonjour SDK redistribution license text |

**Constraints**:
- File is committed to the repository (static, no generation needed).
- Used by NSIS `!insertmacro MUI_PAGE_LICENSE` — must be plain ASCII/UTF-8, no BOM.
- Both sections must be present; omitting either violates FR-005 and §VII.

## State Transitions (installer)

```
[Start]
  └─► Check Bonjour registry key
        ├─► Key PRESENT → skip Bonjour install → proceed to AirBeam install
        └─► Key ABSENT
              └─► Extract BonjourPSSetup.exe to $PLUGINSDIR
                    └─► ExecWait /quiet /norestart
                          ├─► Exit code 0 → proceed to AirBeam install
                          └─► Exit code ≠ 0 → MessageBox → Abort (rollback)
```