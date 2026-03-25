<!--
Sync Impact Report
==================
Version change: 1.4.0 → 1.4.1  (PATCH — Branch protection removed from §CI/CD: solo project, direct pushes to main permitted)
Modified principles: none
Modified sections:
  - CI/CD & Release Pipeline — removed "direct pushes blocked by branch protection" sentence; removed "required status-check context" rationale from job name bullet
Added sections: N/A
Removed sections: N/A
Templates reviewed:
  ✅ .specify/templates/plan-template.md — no structural change required
  ✅ .specify/templates/spec-template.md — no structural change required
  ✅ .specify/templates/tasks-template.md — no structural change required
  ✅ .github/agents/*.agent.md — no outdated references found
Follow-up TODOs:
  - TODO(BMAC_USERNAME): maintainer's Buy Me a Coffee username not yet set in FUNDING.yml
  - TODO(APPCAST_URL): replace <user> placeholder in Auto-Update section with actual GitHub username
-->

# AirBeam Constitution

## Core Principles

### I. Real-Time Audio Thread Safety (NON-NEGOTIABLE)

The audio capture thread (Thread 3) and encoder/sender thread (Thread 4) MUST be real-time safe
at all times during steady-state streaming:

- Heap allocations (`new`, `malloc`, STL containers that allocate) are PROHIBITED on either
  thread; all buffers MUST be pre-allocated before the streaming loop starts.
- Mutex and critical-section locks are PROHIBITED on the hot path; all shared state MUST use
  lock-free primitives (`std::atomic`, SPSC ring buffer).
- Blocking system calls (file I/O, socket send/recv with unbounded waits, `Sleep`,
  `WaitForSingleObject`) are PROHIBITED on Thread 3 or Thread 4.
- The inter-thread buffer between Thread 3 and Thread 4 MUST be a lock-free SPSC ring buffer.
- Thread 3 MUST call `AvSetMmThreadCharacteristics("Audio", …)` before entering the capture loop.

**Rationale**: Audio glitches caused by latency spikes on the capture/encode path are
user-perceptible and cannot be patched retroactively. Real-time safety is an absolute constraint,
not a performance aspiration.

### II. AirPlay 1 / RAOP Protocol Fidelity

AirBeam MUST implement the AirPlay 1 (RAOP) sender protocol correctly and completely for v1.0:

- RTSP session control (TCP), RTP/UDP audio transport, AES-128-CBC per-packet encryption, and
  NTP-like timing synchronization MUST all be implemented and tested end-to-end.
- The retransmit buffer MUST maintain a sliding window of the last 512 RTP packets.
- Volume control MUST be sent via RTSP `SET_PARAMETER` in the −144.0 to 0.0 dB range.
- AirPlay 2 / HomeKit pairing is EXPLICITLY out of scope for v1.0 and MUST NOT be partially
  implemented or stubbed in ways that require breaking changes later.

**Rationale**: Partial protocol implementations produce silent compatibility failures against real
devices and make automated integration testing against shairport-sync unreliable.

### III. Native Win32, No External UI Frameworks

All UI code MUST use the Win32 API (`Shell_NotifyIcon`, `CreatePopupMenu`, balloon notifications)
directly. No external UI frameworks (Qt, wxWidgets, Electron, WPF, WinUI) are permitted. No
kernel-mode code and no audio driver development are in scope for any version.

**Rationale**: A thin, framework-free UI layer minimises binary size, eliminates extra runtime
redistribution, and keeps the application self-contained on stock Windows 10/11.

### III-A. Visual Color Palette (NON-NEGOTIABLE)

The AirBeam UI color palette is strictly:

- **Gray `#9E9E9E`** — idle / inactive state
- **Blue `#2196F3`** — active states: connecting animation and streaming
- **Red `#F44336`** — error states
- **White** — icon foreground detail on colored backgrounds

**Green is explicitly prohibited** in all tray icons, balloon notifications, menus, and any
other UI surface. No new green color values may be introduced in `gen_icons.ps1`, SVG sources,
or any future icon/illustration asset.

**Rationale**: A monochromatic blue-based active palette provides consistent, accessible
signaling across Windows 10/11 dark and light taskbar themes without relying on green, which
conflicts with some system notification colors and reduces color-blind accessibility.

### IV. Test-Verified Correctness

The following tests are MANDATORY before any release build is tagged:

- **Unit**: ALAC encoder round-trip is bit-exact.
- **Unit**: RTP framing produces a valid, parseable packet structure.
- **Unit**: AES-128-CBC output matches known reference vectors.
- **Integration**: WASAPI capture of a known WAV file yields a cross-correlation > 0.99 at the
  encoder output.
- **Integration**: Full RAOP session established and audio streaming successfully against
  shairport-sync (Linux / WSL2 / Docker).
- **End-to-end**: 1 kHz sine wave streamed and verified at the AirPlay receiver.
- **Stress**: 24-hour continuous stream with no memory leaks and no measurable audio drift.

Code changes to the ALAC encoding, RTP framing, or AES encryption paths MUST NOT be merged
without all affected unit tests passing.

**Rationale**: The protocol and encoding layers are deterministic; regressions are fully detectable
by automated tests and MUST be caught before reaching users.

### V. Observable Failures — Never Silent

Every error condition MUST be surfaced to the user via a tray balloon or toast notification.
Silent failure is PROHIBITED. Specifically:

- RTSP connection failures MUST trigger 3 retries with exponential backoff (1 s, 2 s, 4 s) before
  raising a notification.
- Default audio device changes MUST trigger graceful re-attach; a ~50 ms gap is acceptable, but a
  crash or silent stream halt is not.
- A missing Bonjour runtime MUST produce a notification with actionable installation guidance.
- mDNS discovery MUST remove stale entries after a 60 s timeout.

**Rationale**: AirBeam runs as a background tray application with no visible console or log file.
The tray notification is the sole user-facing feedback channel and MUST be reliable.

### VI. Strict Scope Discipline (v1.0)

The following capabilities are EXPLICITLY deferred and MUST NOT be implemented in v1.0:

- AirPlay 2 / HomeKit pairing
- Kernel-mode virtual audio device driver
- Video streaming or screen mirroring
- DLNA or Chromecast output
- Per-application (loopback-isolated) audio capture
- Multi-room simultaneous streaming (single target only in v1.0)

Any proposal to implement a deferred capability MUST result in a formal version-scope discussion
and explicit agreement before work begins.

**Rationale**: Scope creep in audio-protocol software introduces subtle bugs in already-tested
paths. A clean, well-defined v1.0 non-goals list protects quality and release confidence.

### VII. MIT-Compatible Licensing (NON-NEGOTIABLE)

AirBeam is MIT-licensed. All dependencies MUST be license-compatible:

- Apple ALAC encoder (Apache 2.0) — ✅ permitted; statically linked.
- Bonjour SDK / `dnssd.dll` (Apple redistribution terms) — ✅ permitted; dynamically linked.
- WinSparkle (BSD-2-Clause) — ✅ permitted; dynamically linked (`WinSparkle.dll`, prebuilt x64).
- shairport-sync (GPL-3.0) — MUST NOT be copied. May be used only as a read-only protocol
  reference to understand wire-level behaviour.

Any new dependency MUST be reviewed for license compatibility before being added to the project.

**Rationale**: GPL contamination would prevent redistribution of AirBeam under its stated MIT
licence and violates the project's distribution model.

### VIII. Localizable User Interface

Every string visible to the user MUST be externalized into a resource file; hardcoded
user-facing text in source code is PROHIBITED. Specifically:

- All tray menu labels, tray icon tooltips, balloon/toast notification messages, error
  descriptions, and installer UI strings MUST be driven by locale resource files.
- AirBeam MUST ship with complete, reviewed translations for seven languages:
  **en** (English), **de** (German), **fr** (French), **es** (Spanish),
  **ja** (Japanese), **zh-Hans** (Simplified Chinese), **ko** (Korean).
- Adding a new locale MUST require only a new resource file — no code changes permitted.
- The English (en) resource file is the canonical source of truth; all other locales MUST
  cover every key present in the English file with no missing or empty values at release.

**Rationale**: AirPlay-capable receivers are popular in DE, FR, ES, JP, CN, and KR markets.
Reaching users in their native language is essential for global adoption and reduces support
burden. Externalizing strings also makes community-contributed translations straightforward.

## Platform & Toolchain Constraints

- **Target OS**: Windows 10 (build 1903+) and Windows 11, x86-64 only.
- **Compiler**: MSVC 2022 (v143), C++17 standard. Code MUST compile cleanly under `/permissive-`.
  The required toolchain is **Build Tools for Visual Studio 2022** (free, standalone); the full
  Visual Studio IDE is NOT required and MUST NOT be assumed to be present.
- **CMake generator**: **Ninja** with `cl.exe` as compiler (`CMAKE_C_COMPILER=cl.exe`,
  `CMAKE_CXX_COMPILER=cl.exe`). The `"Visual Studio 17 2022"` MSBuild generator is PROHIBITED
  because it requires the full VS IDE installation. CMake presets MUST specify `"generator": "Ninja"`.
- **Build system**: CMake 3.20+. Hand-written `.vcxproj` files outside CMake generation are
  PROHIBITED.
- **Audio API**: WASAPI loopback capture in shared mode, event-driven. Exclusive-mode capture is
  PROHIBITED.
- **Resampling**: libsamplerate or r8brain-free-src MUST be used when the system format differs
  from 44100 Hz stereo S16LE. Custom resampling implementations are PROHIBITED.
- **Auto-update**: WinSparkle (`WinSparkle.dll`, x64 prebuilt, dynamically linked). MUST be
  shipped alongside `AirBeam.exe`. No additional runtime beyond the prebuilt DLL is required.
- **mDNS / DNS-SD**: Bonjour SDK (`dnssd.dll`, dynamic link) or embedded mDNSResponder. Custom
  mDNS stacks are PROHIBITED.
- **Configuration**: `%APPDATA%\AirBeam\config.json`; portable mode overrides with `config.json`
  placed next to the executable.
- **Installer**: NSIS or WiX. MUST detect and offer Bonjour runtime installation if missing.
  Install target: `%PROGRAMFILES%\AirBeam\`.

## Threading Model

The five-thread architecture is fixed and MUST NOT be collapsed, merged, or reordered:

| Thread | Responsibility | Real-Time Safe? |
|--------|---------------|:---------------:|
| 1 — UI / Win32 message loop | Tray icon, context menu, user input | No |
| 2 — mDNS discovery | Bonjour callback loop, device list management | No |
| 3 — Audio capture | WASAPI event-driven, MMCSS-boosted | **YES** |
| 4 — Encoder + RTP sender | ALAC encode, AES encrypt, RTP packetize, UDP send | **YES** |
| 5 — RTSP control | Session setup/teardown, retransmit handler | No |

Cross-thread communication rules:

- **Thread 3 → Thread 4**: SPSC lock-free ring buffer ONLY.
- **All other paths**: Win32 message posting or thread-safe queues; shared state MUST be protected
  by `std::atomic` or a documented lock-free construct.

## CI/CD & Release Pipeline

GitHub Actions is used for **tagged releases** and **PR/push CI builds on non-`main` branches**.
Direct pushes to `main` are permitted — this is a solo project and branch protection is explicitly out of scope for v1.0.

**CI workflow** (triggered by `push` to any branch except `main`, and `pull_request` targeting `main`):
- Builds the `msvc-x64-debug-ci` CMake preset on `windows-latest` (MSVC v143 + Ninja, pre-installed — no install steps)
- Runs `ctest --preset msvc-x64-debug-ci` (unit tests only, `-L unit`)
- Uses per-branch CMake build cache (`actions/cache/restore` + `actions/cache/save`); cleans up build output on `always()`
- Job name is `build-and-test`
- Minimises Actions minutes: debug preset only, no cross-platform matrix, pre-installed toolchain

**Release workflow** (triggered by Git tag matching `vX.Y.Z`):
- Builds Release configuration with full optimisations
- Signs, packages, and publishes installer + portable ZIP
- Updates and deploys `appcast.xml` to `gh-pages`

**Build matrix**: Windows x86-64 only, MSVC 2022 (v143) via Build Tools for Visual Studio 2022, Ninja generator. No cross-platform CI jobs in scope.

**Developer responsibility (pre-tag, local)**:
- Developers MUST run the full unit test suite locally and confirm all tests pass before
  creating a release tag.
- A tag MUST NOT be pushed unless the local build is clean and all unit tests pass.

**Release pipeline (triggered by `vX.Y.Z` tag)**:
1. Compile Release configuration with full optimisations.
2. Package the NSIS installer: `AirBeam-vX.Y.Z-win64-setup.exe`.
3. Package the portable ZIP: `AirBeam-vX.Y.Z-win64-portable.zip`
   (executable + `dnssd.dll` + `WinSparkle.dll` + resource files, no installer required).
4. Sign both artifacts with an OV code-signing certificate if one is available in repository
   secrets; unsigned builds MUST be clearly labelled as such in the GitHub Release notes.
5. Upload both artifacts as GitHub Release assets.
6. Sign the installer with `winsparkle-sign` (or equivalent WinSparkle signing tool) using the
   EdDSA private key stored as an encrypted Actions secret (`SPARKLE_PRIVATE_KEY`). The private
   key MUST NEVER be committed to the repository.
7. Update `appcast.xml` with a new `<item>` entry containing the version string, release notes
   URL, download URL pointing to the GitHub Release asset, file length, and the EdDSA signature
   produced in step 6.
8. Deploy the updated `appcast.xml` to the `gh-pages` branch so it is served at
   `https://TODO(APPCAST_URL).github.io/airbeam/appcast.xml`.

**Release notes**: Generated automatically from conventional commits (`feat:`, `fix:`, `docs:`,
etc.) or replaced by a manually curated `CHANGELOG.md` entry. At minimum the release MUST list
breaking changes and known issues.

## Auto-Update

AirBeam MUST use WinSparkle for over-the-air update delivery. Update checking MUST be enabled by
default and MUST be opt-out only — it MUST NOT be silently disabled by any code path.

- WinSparkle MUST be initialized at tray application startup by calling
  `win_sparkle_set_appcast_url()` followed by `win_sparkle_init()`, before the Win32 message
  loop begins.
- The appcast XML MUST be hosted on the `gh-pages` branch and served at
  `https://TODO(APPCAST_URL).github.io/airbeam/appcast.xml`. The URL MUST be set via
  `win_sparkle_set_appcast_url()` from a compile-time constant; it MUST NOT be user-editable.
- All update packages MUST carry an **EdDSA (Ed25519)** signature embedded in the appcast entry.
  The corresponding Ed25519 public key MUST be embedded in the application's resource file at
  build time. The private signing key MUST exist only as the encrypted Actions secret
  `SPARKLE_PRIVATE_KEY` and MUST NEVER be committed to the repository or logged.
- The tray context menu MUST include a **"Check for Updates"** item that calls
  `win_sparkle_check_update_with_ui()`. This item MUST always be present and functional
  regardless of the `autoUpdate` setting.
- The automatic background check interval is **24 hours** (WinSparkle default). Users MAY
  disable automatic background checks via `config.json` key `"autoUpdate": false`; the manual
  "Check for Updates" tray item MUST remain fully functional when automatic checks are disabled.
- WinSparkle owns the entire update UI (progress dialog, install prompt, restart). AirBeam MUST
  NOT implement its own update overlay or intercept WinSparkle's UI callbacks.

**Rationale**: EdDSA-verified updates ensure users receive patches and new features without
manual download friction. Signature verification protects against supply-chain attacks via a
compromised distribution channel. Keeping auto-update opt-out maximises the population on the
latest version, reducing the long-tail support burden.

## Funding & Community

AirBeam is fully free and open source with no paywalled functionality. Financial sustainability
is pursued through voluntary community support only.

**Funding channels** (configured in `.github/FUNDING.yml`):
- Buy Me a Coffee: `TODO(BMAC_USERNAME)` — set to maintainer's BMAC page slug before first release.
- The BMAC badge MUST appear near the top of `README.md`.

**Community norms**:
- All contributors who land a merged pull request MUST be acknowledged in `CONTRIBUTORS.md`.
- Contribution guidelines MUST be documented in `CONTRIBUTING.md` before the first public release.
- Issues and pull requests MUST be responded to within 14 days by the active maintainer.

## Governance

This constitution supersedes all other project practices, README instructions, and individual
developer preferences wherever they conflict. All pull requests MUST be reviewed against the
principles in this document before merge.

**Amendment procedure**:
1. Open a GitHub issue describing the proposed change and its rationale.
2. Obtain explicit agreement from at least one other contributor (sole maintainer may self-approve
   after a 48-hour reflection period).
3. Update this file, increment the version per the policy below, and set `LAST_AMENDED_DATE` to
   the amendment date.
4. Propagate changes to all dependent templates within the same PR.

**Versioning policy**:
- MAJOR — removal or incompatible redefinition of an existing principle.
- MINOR — new principle or section added, or materially expanded guidance.
- PATCH — clarifications, wording refinements, typo fixes.

**Compliance review**: Every feature spec and implementation plan MUST include a Constitution Check
section validating compliance with all eight principles. Any non-compliance MUST be explicitly
justified and documented in the plan's Complexity Tracking table.

**Version**: 1.4.1 | **Ratified**: 2026-03-21 | **Last Amended**: 2026-03-22
