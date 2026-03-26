# Research: Feature 006 — Bundle Bonjour Installer

## R-001: Apple Bonjour Print Services — Download URL & Package

**Decision**: Use `https://support.apple.com/downloads/DL999/en_US/BonjourPSSetup.exe`

**Rationale**: Apple KB DL999 is the canonical Bonjour Print Services for Windows package.
This is the same URL redistributed by printer manufacturers and commonly bundled with macOS
print utilities on Windows. It installs `dnssd.dll` (64-bit) into System32 and registers
`Bonjour Service` — exactly what AirBeam needs.

**SHA-256**: Must be determined by downloading the file once and running:
```powershell
(Get-FileHash .\BonjourPSSetup.exe -Algorithm SHA256).Hash
```
Pin the resulting hash in `installer/deps/fetch-bonjour.ps1`. The hash is specific to
the pinned version (Bonjour Print Services 3.0.0.10 as of 2024). **This step must be
performed before the first CI run.**

**Alternatives considered**:
- Apple Bonjour SDK (developer kit) — rejected: ships headers + libs, not just the runtime;
  heavier download, not the typical redistributable.
- Bonjour bundled with iTunes — rejected: iTunes full installer is 170+ MB; GPL-adjacent
  bundle; non-redistributable standalone.

---

## R-002: NSIS — Embedding and Running a Nested Installer

**Decision**: Use NSIS `File` directive to embed `BonjourPSSetup.exe` into `$PLUGINSDIR`,
then invoke via `ExecWait` with `/quiet /norestart` switches.

**Rationale**:
- `$PLUGINSDIR` is an NSIS-managed temp directory created at installer startup with a unique
  name; files placed there are automatically cleaned up on exit. No manual temp file management.
- `File /oname=$PLUGINSDIR\BonjourPSSetup.exe "path\to\BonjourPSSetup.exe"` extracts to the
  temp path before the Install section runs.
- `ExecWait` blocks until the subprocess exits and returns the exit code in `$0`. Exit code 0
  = success; any non-zero = failure.
- Under NSIS `RequestExecutionLevel admin`, `ExecWait` inherits the elevated token — no second
  UAC prompt.

**Silent switches for BonjourPSSetup.exe**:
- `/quiet` — suppresses all UI and progress dialogs.
- `/norestart` — suppresses the post-install reboot prompt.
- Pattern: `ExecWait '"$PLUGINSDIR\BonjourPSSetup.exe" /quiet /norestart' $0`

**Rollback pattern**:
```nsis
ExecWait '"$PLUGINSDIR\BonjourPSSetup.exe" /quiet /norestart' $0
${If} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP \
        "Bonjour installation failed — AirBeam cannot continue.$\n$\nError code: $0$\n$\nPlease re-run the installer or install Bonjour manually."
    Abort
${EndIf}
```
`Abort` in NSIS triggers the built-in rollback of any files already installed.

**Alternatives considered**:
- `ExecShell` — rejected: does not block or return an exit code.
- Running MSI directly via `msiexec /i` — rejected: BonjourPSSetup.exe is a bootstrapper
  that handles per-user vs. system install detection; calling the inner MSI directly skips
  this logic.

---

## R-003: CMake Custom Command — Download and Verify a Binary

**Decision**: Use `add_custom_command(OUTPUT ...)` producing `installer/deps/BonjourPSSetup.exe`,
plus `add_custom_target(fetch-bonjour ALL DEPENDS ...)` to hook it into the default build.

**Rationale**:
- `add_custom_command(OUTPUT <file>)` is CMake's standard pattern for generated files: CMake
  skips the command if the output already exists and is up-to-date.
- The fetch script checks the SHA-256 of any existing file first — safe for incremental builds.
- `ALL` on the custom target ensures the file is fetched before the installer is built, even
  in IDEs that don't run all targets by default.
- This is the same pattern as the selected IDE context (`FetchContent.cmake` line 1714):
  `message(FATAL_ERROR "Build step for ${contentName} failed: ${result}")` — our fetch
  script must exit with code 1 on failure so CMake propagates `FATAL_ERROR` correctly.

**CMake snippet**:
```cmake
find_program(POWERSHELL_PROG NAMES pwsh powershell REQUIRED)

add_custom_command(
    OUTPUT  "${CMAKE_SOURCE_DIR}/installer/deps/BonjourPSSetup.exe"
    COMMAND ${POWERSHELL_PROG} -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/installer/deps/fetch-bonjour.ps1"
            -OutputDir "${CMAKE_SOURCE_DIR}/installer/deps"
    COMMENT "Fetching and verifying Bonjour Print Services MSI"
    VERBATIM
)

add_custom_target(fetch-bonjour ALL
    DEPENDS "${CMAKE_SOURCE_DIR}/installer/deps/BonjourPSSetup.exe"
)
```

**Alternatives considered**:
- `file(DOWNLOAD ...)` CMake built-in — rejected: supports MD5/SHA1/SHA256 verification but
  provides no descriptive error message with URL context; error output is harder to diagnose.
- FetchContent — rejected: designed for source code, not binary blobs; creates unwanted
  FetchContent state.

---

## R-004: NSIS — Combined License Page

**Decision**: Replace `!insertmacro MUI_PAGE_LICENSE "..\LICENSE"` with
`!insertmacro MUI_PAGE_LICENSE "..\installer\licenses\combined-license.txt"`.
Create `installer/licenses/combined-license.txt` with both licenses under labeled headers.

**Rationale**:
- Single MUI license page is the standard NSIS pattern; no extra plugin needed.
- Combined `.txt` format is readable in the NSIS scrollable text control.
- Apple's Bonjour SDK redistribution license requires that the license text be included
  with redistribution — showing it on the installer page satisfies this requirement.

**combined-license.txt structure**:
```
=== AirBeam MIT License ===

[AirBeam MIT license text]

=== Apple Bonjour SDK License ===

[Apple Bonjour SDK redistribution license text]
```

---

## R-005: .gitignore Entry

**Decision**: Add `installer/deps/BonjourPSSetup.exe` to `.gitignore` (specific file, not
whole `installer/deps/` directory, so the fetch script itself is tracked).

**Rationale**: Keeping the fetch script tracked while ignoring only the fetched binary
is cleaner than ignoring the whole directory.