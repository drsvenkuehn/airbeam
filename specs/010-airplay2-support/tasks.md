# Tasks: AirPlay 2 Speaker Support

**Input**: Design documents from `specs/010-airplay2-support/`  
**Branch**: `010-airplay2-support`  
**Prerequisites**: plan.md ✅, spec.md ✅, research.md ✅, data-model.md ✅, contracts/ ✅

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: User story label — US1=Stream AP2, US2=Pairing, US3=Multi-room, US4=Backward compat
- All file paths are absolute from repo root

---

## Phase 1: Setup (Dependencies & Project Structure)

**Purpose**: Vendor new crypto dependencies and scaffold the new `src/airplay2/` module.

- [ ] T001 Vendor libsodium to `third_party/libsodium/` as a git submodule pinned to latest stable tag (ISC license confirmed in §VII)
- [ ] T002 Vendor csrp single-file `srp.c` + `srp.h` to `third_party/csrp/` with BSD-2/MIT license header verified
- [ ] T003 Update root `CMakeLists.txt`: add `third_party/libsodium/` and `third_party/csrp/` targets; link `csrp` against `OpenSSL::Crypto`; add `Bcrypt.lib` to `AirBeam` link set
- [ ] T004 Update `tests/CMakeLists.txt` to link test executables against libsodium and csrp
- [ ] T005 [P] Create `src/airplay2/` directory with stub `.gitkeep` and an empty `CMakeLists.txt` placeholder; add `add_subdirectory(airplay2)` to root `src/CMakeLists.txt`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core AirPlay 2 infrastructure that ALL user stories depend on. No user story work can begin until this phase is complete.

**⚠️ CRITICAL**: Complete and validate this phase before starting Phases 3–6.

- [ ] T006 Extend `src/discovery/AirPlayReceiver.h`: add `supportsAirPlay2` (bool), `hapDevicePublicKey` (std::string, base64 Ed25519 from TXT `pk`), `pairingState` (PairingState enum), `airPlay2Port` (uint16_t); add `PairingState` enum with values `NotApplicable / Unpaired / Pairing / Paired / Error`
- [ ] T007 Update `src/discovery/TxtRecord.h/.cpp`: parse `pk` TXT field into `hapDevicePublicKey`; parse `vv` field; set `supportsAirPlay2 = !pk.empty()`; set `isAirPlay2Only = supportsAirPlay2 && et_has_no_rsa()`; set `airPlay2Port` from TXT-advertised port (fallback 7000)
- [ ] T008 Update `src/discovery/ReceiverList.h/.cpp`: merge logic correctly handles receivers with both `supportsAirPlay2` and `isAirPlay1Compatible` set; stale-entry removal (60 s) applies equally to AP2 receivers
- [ ] T009 Add `WM_AP2_PAIRING_REQUIRED`, `WM_AP2_PAIRING_STALE`, `WM_AP2_CONNECTED`, `WM_AP2_FAILED`, `WM_AP2_SPEAKER_DROPPED` constants to `src/core/Messages.h` following existing WM_RAOP_* naming convention
- [ ] T010 [P] Implement `src/airplay2/CredentialStore.h/.cpp`: `EnsureControllerIdentity()`, `Read()`, `Write()`, `Delete()`, `DeviceIdFromPublicKey()` exactly per `contracts/credential-store.md`; use `CredWriteW`/`CredReadW`/`CredDeleteW` (Advapi32.lib); JSON blob format per schema; version field mandatory
- [ ] T011 [P] Add `tests/unit/test_credential_store.cpp`: write/read/delete round-trip for `PairingCredential`; verify controller identity is stable across multiple `EnsureControllerIdentity()` calls; verify `DeviceIdFromPublicKey()` produces 12-char hex

**Checkpoint**: Phase 2 complete when `test_credential_store` passes and mDNS discovery correctly identifies AirPlay 2 receivers in the UI (they appear but remain greyed-out/unpaired).

---

## Phase 3: User Story 2 — One-Time Pairing Flow (Priority: P1)

**Goal**: Users can pair AirBeam with any AirPlay 2 speaker through a guided, self-contained flow — no Apple device or Home app required.

**Independent Test**: Factory-reset an AirPlay 2 device → launch AirBeam → select device → pairing ceremony completes → device appears as normal selectable speaker in tray menu.

- [ ] T012 [P] [US2] Implement `src/airplay2/HapPairing.h/.cpp`: full HAP SRP-6a + Ed25519 + Curve25519 + ChaCha20-Poly1305 ceremony (M1→M6 per research.md §2 pairing flow); use libsodium for Ed25519/X25519/ChaCha20, csrp for SRP-6a; on success call `CredentialStore::Write()`; post `WM_AP2_PAIRING_REQUIRED` on start, nothing on success (caller polls pairingState)
- [ ] T013 [P] [US2] Add `tests/unit/test_hap_pairing.cpp`: verify SRP-6a M1/M2 proof exchange with known test vectors; verify Ed25519 sign+verify round-trip using libsodium; verify ChaCha20-Poly1305 encrypt+decrypt of simulated M5/M6 frames; test stale-credential detection (rejected M2 triggers re-pair path)
- [ ] T014 [US2] Implement PIN dialog for Apple TV: add `src/ui/PinDialog.h/.cpp` as a Win32 modal `DialogBox` (6-digit numeric input); invoked when `HapPairing` receives `kTLVError_Authentication` indicating PIN is required; posts PIN back to `HapPairing` via `WM_APP` message; uses system font, no external UI framework (§III)
- [ ] T015 [US2] Handle `WM_AP2_PAIRING_REQUIRED` in `src/core/AppController.h/.cpp`: start `HapPairing` worker, show tray balloon "Pairing with {DeviceName}…" (blue, §III-A); update receiver `pairingState = Pairing` in `ReceiverList`
- [ ] T016 [US2] Handle pairing success in `AppController`: update receiver `pairingState = Paired`; show balloon "Paired with {DeviceName}" (blue); rebuild tray menu so device appears as normal selectable item
- [ ] T017 [US2] Handle pairing failure in `AppController`: update `pairingState = Error`; show tray balloon with failure reason and suggested action (§V); retry trigger via menu re-selection
- [ ] T018 [US2] Handle `WM_AP2_PAIRING_STALE` in `AppController`: auto-delete stale credential via `CredentialStore::Delete()`; show balloon "Device was reset — re-pairing required" (red, §III-A); auto-restart pairing flow (no user action needed — per clarification Q2)
- [ ] T019 [P] [US2] Add "Forget device" menu item to `src/ui/TrayMenu.h/.cpp` and `src/ui/CustomPopup.h/.cpp`: appears in right-click submenu (or as a submenu item) for paired AP2 speakers only; calls `CredentialStore::Delete()` and sets `pairingState = Unpaired`
- [ ] T020 [P] [US2] Add all new user-facing strings to `src/localization/` for all 7 locales (en, de, fr, es, ja, zh-Hans, ko): pairing-start notification, pairing-success notification, pairing-failure notification, stale-credential notification, PIN prompt label, "Forget device" label, "Paired" / "Pairing…" / "Unpaired" state labels — per §VIII

**Checkpoint**: Phase 3 complete when a real AirPlay 2 device can be paired from scratch (without Apple Home app) and appears as a normal selectable item in the tray menu.

---

## Phase 4: User Story 1 — Stream to AirPlay 2-Only Speaker (Priority: P1)

**Goal**: A previously paired AirPlay 2 speaker can be selected from the tray menu and audio starts playing within 3 seconds.

**Independent Test**: Select a paired HomePod from tray menu → audio plays within 3 s → volume slider controls speaker volume → Disconnect stops stream cleanly → auto-reconnect on startup works.

**Prerequisite**: Phase 3 (pairing) must be complete and validated.

- [ ] T021 [P] [US1] Implement `src/airplay2/AesGcmCipher.h/.cpp`: AES-128-GCM encrypt/decrypt using Windows BCrypt (`BCRYPT_CHAIN_MODE_GCM`); per-packet nonce derivation (session_salt[4] ‖ rtp_seq[4] padded); pre-allocated BCRYPT handles in ctor (RT-safe on Thread 4 — §I); zero heap on hot path
- [ ] T022 [P] [US1] Add `tests/unit/test_aes_gcm.cpp`: encrypt+decrypt round-trip with known test vectors; verify 16-byte auth tag appended correctly; verify nonce increments per packet; verify tampered ciphertext fails auth tag check
- [ ] T023 [P] [US1] Implement `src/airplay2/PtpClock.h/.cpp`: PTP SYNC packet send/receive loop on UDP timing socket; clock offset calculation using `QueryPerformanceCounter()` for nanosecond precision; `SetReferenceOffset()` and `ClockOffset()` accessors used by `MultiRoomCoordinator`
- [ ] T024 [US1] Implement `src/airplay2/AirPlay2Session.h/.cpp` per `contracts/airplay2-session-api.md`: `Init()` (load credential, derive session key via HKDF-SHA256, open WinHTTP HTTP/2 session with TLS + ALPN "airplay"); `StartRaop()` (HTTP/2 SETUP POST to `/audio`); `StopRaop()` (HTTP/2 TEARDOWN); `SetVolume()` (HTTP/2 POST to `/controller`); audio RTP via `AesGcmCipher`; timing via `PtpClock`; posts `WM_AP2_CONNECTED` / `WM_AP2_FAILED`
- [ ] T025 [US1] Update `src/core/ConnectionController.h/.cpp`: add protocol-selection logic — if `receiver.supportsAirPlay2 && IsPaired(receiver)` construct `AirPlay2Session`; if `receiver.supportsAirPlay2 && !IsPaired(receiver)` post `WM_AP2_PAIRING_REQUIRED` and abort; otherwise construct existing `StreamSession` (AirPlay 1 — unchanged)
- [ ] T026 [US1] Handle `WM_AP2_CONNECTED` in `src/core/AppController.h/.cpp`: update tray icon to blue streaming state; update menu item to checked state; enable volume slider
- [ ] T027 [US1] Handle `WM_AP2_FAILED` in `AppController`: implement 3-retry exponential backoff (1 s, 2 s, 4 s) matching AirPlay 1 reconnect behaviour; show tray notification on final failure (§V)
- [ ] T028 [US1] Implement auto-reconnect to last AP2 device on startup in `AppController::OnStartup()`: check `config_.lastDevice` against discovered AP2 receivers within 5-second window; if discovered and paired, call `BeginConnect()` automatically — matching v1.0 AirPlay 1 auto-reconnect (FR-014)
- [ ] T029 [P] [US1] Add `tests/integration/test_airplay2_session.cpp`: full AP2 session setup against a shairport-sync instance compiled with AP2 support (Docker/WSL2); verify SETUP succeeds, audio flows, SET_PARAMETER (volume) accepted, TEARDOWN clean

**Checkpoint**: Phase 4 complete when a paired AirPlay 2 speaker plays audio from AirBeam — SC-001 (no Apple device needed), SC-003 (≤3 s to audio), SC-004 (≤2 s latency) all verified.

---

## Phase 5: User Story 3 — Multi-Room Streaming (Priority: P2)

**Goal**: Two or more paired AirPlay 2 speakers play synchronized audio simultaneously (< 10 ms offset).

**Independent Test**: Select 2+ paired AP2 speakers in tray menu → both play audio simultaneously → cross-room echo is imperceptible → one speaker drops without interrupting the others.

**Prerequisite**: Phase 4 (single-speaker streaming) must be complete and validated.

- [ ] T030 [P] [US3] Update `src/ui/CustomPopup.h/.cpp`: AP2 speakers render as checkbox items (multi-check toggle — independent selection); AP1 speakers retain radio-button semantics; add `PopupItemType::CheckboxItem` variant to the `PopupItem` struct; render blue checkbox glyph for checked AP2 items (§III-A)
- [ ] T031 [US3] Update `src/ui/TrayMenu.h/.cpp` `BuildItems()`: implement protocol-exclusive switching logic — selecting an AP1 speaker calls `AppController::DeactivateAllAP2Speakers()` before activating the AP1 speaker; selecting any AP2 speaker calls `AppController::DeactivateAP1Speaker()` (per clarification Q3)
- [ ] T032 [P] [US3] Add `tests/unit/test_multi_select_menu.cpp`: verify selecting AP1 clears all AP2 active; verify selecting AP2 clears AP1; verify up to 6 AP2 speakers can be simultaneously checked; verify 7th AP2 selection is rejected
- [ ] T033 [US3] Implement `src/airplay2/MultiRoomCoordinator.h/.cpp`: holds up to 6 `AirPlay2Session` instances; fan-out from single ALAC encoder output to all sessions (Thread 4 writes to each session's RTP sender); shared PTP reference clock (`PtpClock::SetReferenceOffset()` per session); group volume setter (`SetGroupVolume()` iterates sessions)
- [ ] T034 [US3] Update `src/core/AppController.h/.cpp`: when >1 AP2 speaker activated, construct `MultiRoomCoordinator` and hand sessions to it; when only 1 AP2 active, use single `AirPlay2Session` directly; handle `WM_AP2_SPEAKER_DROPPED` in multi-room context (remove dropped session, continue others, show balloon)
- [ ] T035 [P] [US3] Add group volume + per-speaker volume to `src/ui/CustomPopup.h/.cpp`: global volume slider at top of menu (affects all active speakers); per-speaker volume accessible via submenu (right-arrow or secondary popup); both sliders call `MultiRoomCoordinator` volume setters
- [ ] T036 [P] [US3] Add `tests/unit/test_ptp_clock.cpp`: clock offset calculation with simulated RTT; verify `< 10 ms` offset maintained across 6 sessions with synthetic clock skew; verify `SetReferenceOffset` correctly adjusts per-session PTP base
- [ ] T037 [US3] Add multi-room locale strings to `src/localization/` for all 7 locales: "Streaming to N speakers", "Speaker dropped: {Name}", "Group volume", per-speaker volume label — §VIII

**Checkpoint**: Phase 5 complete when 2+ HomePods play synchronised audio with no audible echo — SC-005 (< 10 ms offset) verified.

---

## Phase 6: User Story 4 — Backward Compatibility with AirPlay 1 (Priority: P3)

**Goal**: All v1.0 AirPlay 1 functionality works exactly as before with zero regressions.

**Independent Test**: Run the full v1.0 unit + integration test suite against an AirPlay 1 receiver (shairport-sync) — all tests pass without modification.

- [ ] T038 [US4] Run full v1.0 unit test suite (`ctest --preset msvc-x64-debug-ci -L unit`) — verify all pre-existing tests pass; fix any regressions introduced by AP2 code changes
- [ ] T039 [US4] Run integration test against shairport-sync in AirPlay 1 mode — verify ANNOUNCE/SETUP/RECORD/TEARDOWN sequence works; verify AES-CBC encryption unchanged; verify NTP timing unchanged
- [ ] T040 [P] [US4] Verify dual-protocol receiver behaviour in `ConnectionController`: a receiver with both `isAirPlay1Compatible` and `supportsAirPlay2` true uses `AirPlay2Session` if paired; falls back to `StreamSession` (AirPlay 1) if not paired — per FR-020; add unit test for this selection logic in `tests/unit/test_connection_controller.cpp`

**Checkpoint**: Phase 6 complete when ALL v1.0 tests pass unchanged — SC-007 verified.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Final validation gates, stress test, and spec compliance sweep.

- [ ] T041 [P] Verify all 7 locale files in `src/localization/` are complete with no missing or empty keys introduced by this feature — run locale completeness check (diff key sets against `en` canonical)
- [ ] T042 [P] Verify §III-A color palette compliance across all new UI surfaces: pairing-in-progress uses blue, errors use red, no green introduced; inspect `CustomPopup.cpp` and `BalloonNotify` calls added in this feature
- [ ] T043 Run 24-hour stress test against a real AirPlay 2 speaker: continuous stream, monitor for memory growth (< 10 MB above baseline), audio dropout, and crash — SC-008 verification
- [ ] T044 [P] Update `specs/010-airplay2-support/checklists/requirements.md` to mark all FRs verified
- [ ] T045 [P] Update `README.md` to document AirPlay 2 support, pairing instructions, and supported devices under the Features section

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 — **BLOCKS all user story phases**
- **Phase 3 (US2 Pairing)**: Depends on Phase 2 — can begin once foundational complete
- **Phase 4 (US1 Streaming)**: Depends on Phase 3 — pairing credential must exist to stream
- **Phase 5 (US3 Multi-room)**: Depends on Phase 4 — single-speaker must work first
- **Phase 6 (US4 Backward Compat)**: Depends on Phase 4 — regression test after all AP2 code added
- **Phase 7 (Polish)**: Depends on Phases 3–6 complete

### User Story Dependencies

- **US2 (Pairing, P1)**: After Phase 2 — blocks US1
- **US1 (Stream AP2, P1)**: After US2 — the primary P1 deliverable
- **US3 (Multi-room, P2)**: After US1 — sequential; P2 ships as follow-on
- **US4 (Backward Compat, P3)**: After Phase 4 — regression validation

### Parallel Opportunities

Within Phase 2: T006, T007, T008, T009, T010, T011 can all run in parallel (different files).

Within Phase 3: T012 (HapPairing impl), T013 (HapPairing tests), T014 (PinDialog), T019 (Forget device), T020 (locale strings) can all run in parallel.

Within Phase 4: T021 (AesGcmCipher), T022 (AES tests), T023 (PtpClock), T029 (integration test) can all run in parallel; T024 (AirPlay2Session) depends on T021 + T023.

---

## Parallel Example: Phase 3 (Pairing)

```
# Launch these in parallel (all independent files):
Task T012: src/airplay2/HapPairing.h/.cpp
Task T013: tests/unit/test_hap_pairing.cpp
Task T014: src/ui/PinDialog.h/.cpp
Task T020: src/localization/ (all 7 locale files)

# Then sequentially (depends on T012):
Task T015: AppController — WM_AP2_PAIRING_REQUIRED handler
Task T016: AppController — pairing success
Task T017: AppController — pairing failure
Task T018: AppController — WM_AP2_PAIRING_STALE
```

## Parallel Example: Phase 4 (Streaming)

```
# Launch these in parallel:
Task T021: src/airplay2/AesGcmCipher.h/.cpp
Task T022: tests/unit/test_aes_gcm.cpp
Task T023: src/airplay2/PtpClock.h/.cpp
Task T029: tests/integration/test_airplay2_session.cpp (scaffold)

# Then sequentially (T024 depends on T021 + T023):
Task T024: src/airplay2/AirPlay2Session.h/.cpp
Task T025: src/core/ConnectionController — protocol selection
Task T026: AppController — WM_AP2_CONNECTED
Task T027: AppController — WM_AP2_FAILED (backoff)
Task T028: AppController — auto-reconnect on startup
```

---

## Implementation Strategy

### MVP: User Stories 2 + 1 (P1 only — phases 1–4)

1. Complete Phase 1 (Setup) — vendor dependencies
2. Complete Phase 2 (Foundational) — AP2 detection, credential store
3. Complete Phase 3 (US2 Pairing) — pair one device
4. Complete Phase 4 (US1 Streaming) — stream to one paired AP2 speaker
5. **STOP and VALIDATE**: SC-001, SC-003, SC-004 met → ship P1 update

### Incremental Delivery

1. Phases 1–4 → P1 (single AP2 speaker) → release `v1.1.0`
2. Phase 5 (US3 Multi-room) → P2 → release `v1.2.0`
3. Phase 6 (US4 validation) → P3 → baked into each release
4. Phase 7 (Polish) → baked into final release milestone

---

## Notes

- [P] tasks = different files, safe to parallelize
- [Story] label maps task to user story for traceability
- All tests are unit/integration tests required by constitution §IV gate — not optional for crypto paths
- Kill AirBeam before building: `Stop-Process -Name AirBeam -ErrorAction SilentlyContinue`
- Build both configs: `ninja -C build\msvc-x64-debug AirBeam && ninja -C build\msvc-x64-release AirBeam`
- **Never copy GPL code from shairport-sync** — use only as read-only protocol reference (§VII)
- Locale strings (§VIII): every new user-visible string must appear in all 7 locale files before merge
