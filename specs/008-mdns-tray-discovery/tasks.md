# Tasks: mDNS Discovery and Tray Speaker Menu

**Feature**: `008-mdns-tray-discovery`  
**Branch**: `008-mdns-tray-discovery`  
**Input**: `specs/008-mdns-tray-discovery/` — spec.md, plan.md, data-model.md, research.md, contracts/, quickstart.md  
**Prerequisites**: MSVC 2022 v143, CMake 3.20+, Ninja. All existing unit tests pass before starting:
`ctest --preset msvc-x64-debug-ci -L unit`

## Format: `[ID] [P?] [Story?] Description with file path`

- **[P]**: Can run in parallel (different files, no incomplete dependencies)
- **[Story]**: Which user story ([US1]–[US5]) — required for all Phase 3+ tasks
- Exact file paths are based on the repository layout verified at task-generation time

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Add the four new resource IDs and locale strings that every subsequent phase
depends on; fix the two header-level data-model changes that propagate through all compilation
units.

**⚠️ T001 must complete before T002–T008 (resource_ids.h defines the IDs used by all
locale files). T009 and T010 can start immediately; both are independent headers.**

- [x] T001 Add `IDS_MENU_SEARCHING` (1028), `IDS_MENU_BONJOUR_MISSING` (1029), `IDS_MENU_CONNECTING` (1030), `IDS_MENU_SPEAKERS` (1031) to `resources/resource_ids.h` in the `// ── Speaker menu dynamic strings` section (per contracts/menu-command-ids.md §New Resource IDs)
- [x] T002 Add 4-entry STRINGTABLE block to `resources/locales/strings_en.rc`: `IDS_MENU_SEARCHING "Searching for speakers\x2026"`, `IDS_MENU_BONJOUR_MISSING "Install Bonjour to discover speakers"`, `IDS_MENU_CONNECTING " \x2014 Connecting\x2026"`, `IDS_MENU_SPEAKERS "Speakers"` (English canonical values; depends on T001)
- [x] T003 [P] Add same 4 IDs with `[TBD]` placeholder translations to `resources/locales/strings_de.rc` (German; depends on T001, parallel with T004–T008)
- [x] T004 [P] Add same 4 IDs with `[TBD]` placeholder translations to `resources/locales/strings_fr.rc` (French)
- [x] T005 [P] Add same 4 IDs with `[TBD]` placeholder translations to `resources/locales/strings_es.rc` (Spanish)
- [x] T006 [P] Add same 4 IDs with `[TBD]` placeholder translations to `resources/locales/strings_ja.rc` (Japanese)
- [x] T007 [P] Add same 4 IDs with `[TBD]` placeholder translations to `resources/locales/strings_zh-Hans.rc` (Simplified Chinese)
- [x] T008 [P] Add same 4 IDs with `[TBD]` placeholder translations to `resources/locales/strings_ko.rc` (Korean)
- [x] T009 [P] Fix `AirPlayReceiver::lastSeenTick` from `DWORD` to `ULONGLONG`; add `std::wstring stableId` field (stable MAC-based device ID); add `/// stableId: MAC prefix from instance name` doc comment in `src/discovery/AirPlayReceiver.h` (R-007, R-001; no other files change in this task)
- [x] T010 [P] Add private members `sortedReceivers_` (`std::vector<AirPlayReceiver>`), `connectedReceiverIdx_` (`int = -1`), `connectingReceiverIdx_` (`int = -1`), `priorConnectedIdx_` (`int = -1`), `bonjourMissing_` (`bool = false`), `suppressNextRaopFailed_` (`bool = false`), and `static constexpr UINT TIMER_HANDSHAKE_TIMEOUT = 3` to `AppController` in `src/core/AppController.h` (R-011; parallel with T009)

**Checkpoint**: Project compiles with updated headers and all 7 locale files have 4 new string entries.

---

## Phase 2: Foundational (Discovery Pipeline)

**Purpose**: Wire the three low-level data-pipeline changes that all user stories depend on:
stable-ID extraction, TXT `an`-field display-name construction, and AirPlay 1 filter enforcement.
All three tasks touch different files and can run in parallel.

**⚠️ CRITICAL**: Phase 1 must be complete (T009 AirPlayReceiver.h fields available) before
this phase can compile.

- [x] T011 [P] Add `DeviceIdFromInstance(const std::wstring&)` helper to the anonymous namespace in `src/discovery/MdnsDiscovery.cpp`; call it in `BrowseCallback` to set `pendingResolve_.receiver.stableId`; apply `std::transform` to uppercase the extracted MAC string before storing as `stableId` (handles Bonjour instances that return lowercase hex); change `AddrInfoCallback` to call `GetTickCount64()` instead of `GetTickCount()` for `r.lastSeenTick` (R-001, R-007; quickstart §Step 2)
- [x] T012 [P] Add `an` field branch to the TXT-record parse loop in `src/discovery/TxtRecord.cpp`; after the loop, build `displayName` from `an + " (" + am + ")"` using `Utf8ToWide`; apply 40-char truncation with `L"\u2026"` suffix per the display-name construction algorithm in `contracts/txt-record-parsing.md §Display Name Truncation Specification` (R-002; quickstart §Step 3)
- [x] T013 [P] Add early-return guard `if (!receiver.isAirPlay1Compatible) return;` at the top of `ReceiverList::Update()` in `src/discovery/ReceiverList.cpp` to silently discard AirPlay 2 devices before upsert (R-008; quickstart §Step 4)

**Checkpoint**: `TxtRecord::Parse` populates `displayName` from `an`/`am`; `ReceiverList::Update` rejects non-AirPlay-1 receivers; `stableId` is populated in the browse pipeline. The project builds cleanly.

---

## Phase 3: User Story 1 — Automatic Speaker Discovery (Priority: P1) 🎯 MVP

**Goal**: Discovered AirPlay 1 speakers appear in the tray menu within 10 seconds of launch.
Bonjour-missing and searching placeholders are shown when appropriate.

**Independent Test**: Launch AirBeam with ≥1 AirPlay 1 receiver on the LAN. Right-click tray
within 10 s → speaker name appears in menu. See quickstart.md §Manual Testing items 1–4.

**Depends on**: Phase 2 complete.

- [x] T014 [US1] Complete the Bonjour browse/resolve/addrinfo pipeline in `src/discovery/MdnsDiscovery.cpp`: ensure `BrowseCallback` seeds `pendingResolve_`, `ResolveCallback` calls `TxtRecord::Parse`, and `AddrInfoCallback` calls `ReceiverList::Update()` with the fully-populated `AirPlayReceiver`; ensure the remove-event path calls `ReceiverList::Remove(instanceName)` — this is the DNS-SD backbone for all discovery user stories (Note: if `src/discovery/BonjourBrowser.h/.cpp` stubs were created on this branch but do not yet exist, create them here and wire them to `MdnsDiscovery`; per plan.md the canonical implementation target is `src/discovery/MdnsDiscovery.cpp`)
- [x] T015 [US1] Rewrite `AppController::HandleReceiversUpdated` in `src/core/AppController.cpp`: (1) call `receiverList_->Snapshot()`, (2) `std::sort` by `displayName` (`wstring::operator<`), (3) store as `sortedReceivers_`, (4) re-derive `connectedReceiverIdx_` by `instanceName` match; if connected receiver not found call `Disconnect()` and clear `connectedReceiverIdx_ = -1` (R-009; quickstart §Step 9 §HandleReceiversUpdated)
- [x] T016 [US1] Update `AppController::HandleBonjourMissing` in `src/core/AppController.cpp` to set `bonjourMissing_ = true` in addition to the existing balloon notification (contracts/wm-messages.md §WM_BONJOUR_MISSING; quickstart §Step 9 §HandleBonjourMissing)
- [x] T017 [US1] Update `TrayMenu::Show` signature in `src/ui/TrayMenu.h` and `src/ui/TrayMenu.cpp`: add `bool bonjourMissing`, `const std::vector<AirPlayReceiver>& receivers`, `int connectedReceiverIdx`, `int connectingReceiverIdx` parameters per contracts/menu-command-ids.md §TrayMenu::Show; implement the `bonjourMissing` placeholder (`IDS_MENU_BONJOUR_MISSING`, grayed) and empty-receivers placeholder (`IDS_MENU_SEARCHING`, grayed) branches — speaker rendering will be completed in T022 (quickstart §Step 6–7)
- [x] T018 [US1] Update `AppController::ShowTrayMenu` call site in `src/core/AppController.cpp` to pass `bonjourMissing_`, `sortedReceivers_`, `connectedReceiverIdx_`, `connectingReceiverIdx_` to `trayMenu_.Show(...)` per the new signature (quickstart §Step 9 §ShowTrayMenu)
- [x] T019 [P] [US1] Create `tests/unit/test_receiver_list.cpp` with GoogleTest cases: `ReceiverList.Update_AirPlay1_Accepted`, `ReceiverList.Update_AirPlay2_Rejected`, `ReceiverList.PruneStale_RemovesOldEntries`, `ReceiverList.PruneStale_KeepsRecentEntries`, `ReceiverList.Remove_PostsUpdated`, `ReceiverList.Snapshot_ReturnsFilteredCopy` (plan.md §Testing; data-model.md §ReceiverList)
- [x] T020 [P] [US1] Extend `tests/unit/test_mdns_txt.cpp` with 5 new test cases: `TxtRecord.AnField_SetsDisplayName`, `TxtRecord.AnAndAm_CombinedDisplayName`, `TxtRecord.LongName_Truncated` (50-char name → 40 chars + `…`), `TxtRecord.NoAn_FallbackPreserved`, `DeviceIdFromInstance_LowercaseInput_ReturnsUppercase` (contracts/txt-record-parsing.md §Unit Test Coverage)
- [x] T021 [US1] Register new test file and source in `tests/CMakeLists.txt`: add `unit/test_receiver_list.cpp` to `UNIT_TEST_SOURCES`; add `${CMAKE_SOURCE_DIR}/src/discovery/ReceiverList.cpp` to `TEST_CORE_SOURCES` so `ReceiverList` methods are available in the test binary

**Checkpoint**: `ctest --preset msvc-x64-debug-ci -L unit -R "txt|receiver_list"` passes. Manual
test: right-click tray within 10 s of launch with a live AirPlay 1 speaker → name appears.
"Searching for speakers…" shown when no devices present.

---

## Phase 4: User Story 2 — Select a Speaker to Stream To (Priority: P1)

**Goal**: Clicking a speaker initiates a pessimistic handshake. Checkmark commits only on
confirmed `WM_RAOP_CONNECTED`. Handshake timeout or failure reverts silently. 3-vs-4
inline/submenu threshold enforced in the menu.

**Independent Test**: With ≥2 AirPlay 1 receivers in menu, click one → "Connecting…" label
appears immediately; on success checkmark moves to it. See quickstart.md §Manual Testing items
6–7.

**Depends on**: Phase 3 complete (TrayMenu signature established by T017).

- [x] T022 [US2] Complete the speaker-item rendering section in `src/ui/TrayMenu.cpp`: when `receivers.size() <= 3` append speaker items inline; when `receivers.size() >= 4` create submenu via `CreatePopupMenu()`, populate it, then attach with `MF_POPUP | MF_STRING` using `IDS_MENU_SPEAKERS`; for each item: start with `displayName`, append `StringLoader::Load(IDS_MENU_CONNECTING)` if `i == connectingReceiverIdx`, add `MF_CHECKED` if `i == connectedReceiverIdx`, use `IDM_DEVICE_BASE + i` as command ID; destroy the old grayed-out AirPlay2 display code if present (contracts/menu-command-ids.md §Menu Structure Specification; quickstart §Step 7)
- [x] T023 [US2] Implement `IDM_DEVICE_BASE` dispatch in `AppController::HandleCommand` in `src/core/AppController.cpp`: bounds check `idx < sortedReceivers_.size()`; idempotent guard `if (isConnected_ && idx == connectedReceiverIdx_) return`; cancel-and-redirect if `connectingReceiverIdx_ >= 0` (kill timer, set `suppressNextRaopFailed_ = true` before calling `raopSession_->Stop()`, then reset `suppressNextRaopFailed_ = false` after stop/reset of raopSession_); otherwise save `priorConnectedIdx_ = connectedReceiverIdx_`; set `connectingReceiverIdx_ = idx`, call `Connect(sortedReceivers_[idx])`, `SetTimer(hwnd_, TIMER_HANDSHAKE_TIMEOUT, 5000, nullptr)` (contracts/menu-command-ids.md §HandleCommand)
- [x] T024 [US2] Update `AppController::HandleRaopConnected` in `src/core/AppController.cpp`: kill `TIMER_HANDSHAKE_TIMEOUT`; set `connectedReceiverIdx_ = connectingReceiverIdx_`; set `connectingReceiverIdx_ = -1`; move `isConnected_ = true` from `Connect()` to here; then proceed with existing WASAPI/ALAC startup (contracts/wm-messages.md §WM_RAOP_CONNECTED; R-005)
- [x] T025 [US2] Update `AppController::HandleRaopFailed` in `src/core/AppController.cpp`: at the very top of the handler, check `if (suppressNextRaopFailed_) { suppressNextRaopFailed_ = false; return; }` before any other logic; then if `connectingReceiverIdx_ >= 0` (pessimistic path) — kill `TIMER_HANDSHAKE_TIMEOUT`, restore `connectedReceiverIdx_ = priorConnectedIdx_`, set `connectingReceiverIdx_ = -1`, do NOT show balloon (silent revert per FR-016b); else (mid-stream path) — apply existing retry logic with balloon (contracts/wm-messages.md §WM_RAOP_FAILED; R-005)
- [x] T026 [US2] Add `TIMER_HANDSHAKE_TIMEOUT` case to `AppController::HandleTimer` in `src/core/AppController.cpp`: on timeout perform the same revert as the pessimistic path in T025 — kill timer, restore `priorConnectedIdx_`, clear `connectingReceiverIdx_` (R-005; data-model.md §SpeakerMenuState state transitions)
- [x] T027 [US2] Remove `isConnected_ = true` from `AppController::Connect()` in `src/core/AppController.cpp` — this line now lives in `HandleRaopConnected` (T024); confirm no other early-return paths in `Connect()` prematurely set `isConnected_` (R-005; contracts/wm-messages.md §WM_RAOP_CONNECTED Contract)
- [x] T028 [P] [US2] Create `tests/unit/test_tray_menu.cpp` with 7 GoogleTest cases: `TrayMenu.BonjourMissing_ShowsInstallItem`, `TrayMenu.EmptyReceivers_ShowsSearching`, `TrayMenu.ThreeSpeakers_Inline`, `TrayMenu.FourSpeakers_Submenu`, `TrayMenu.ConnectedIdx_ShowsCheckmark`, `TrayMenu.ConnectingIdx_ShowsLabel`, `TrayMenu.AlphaOrder_Preserved`; use headless Win32 approach (`CreatePopupMenu` + `GetMenuItemInfo`, no visible window) (contracts/menu-command-ids.md §Unit Test Coverage)
- [x] T029 [US2] Register new test file and source in `tests/CMakeLists.txt`: add `unit/test_tray_menu.cpp` to `UNIT_TEST_SOURCES`; add `${CMAKE_SOURCE_DIR}/src/ui/TrayMenu.cpp` and `${CMAKE_SOURCE_DIR}/src/localization/StringLoader.cpp` to `TEST_CORE_SOURCES` so TrayMenu and string loading are available in the test binary

**Checkpoint**: `ctest --preset msvc-x64-debug-ci -L unit -R "tray_menu"` passes all 7 cases.
Manual: click speaker → "Connecting…" label appears; on success checkmark moves; on 5 s timeout
menu silently reverts. 4-speaker submenu appears correctly.

---

## Phase 5: User Story 3 — Resume Last-Used Speaker After Restart (Priority: P2)

**Goal**: On startup, if `config_.lastDevice` matches a speaker discovered within 30 seconds,
auto-connect without user interaction. Window expires at 30 s.

**Independent Test**: Select a speaker, close AirBeam, restart while the speaker is on the
network → auto-connected within 10 s with checkmark, zero user interaction.
See quickstart.md §Manual Testing items 9–10.

**Depends on**: Phase 4 complete (Connect() pessimistic flow, HandleRaopConnected, HandleRaopFailed).

- [x] T030 [US3] Fix `TIMER_RECONNECT_WINDOW` duration from `5000` to `30000` ms in `AppController::Start` in `src/core/AppController.cpp` (R-006; spec §FR-019 SC-006 — 30-second auto-select window)
- [x] T031 [US3] Add auto-select logic to `AppController::HandleReceiversUpdated` in `src/core/AppController.cpp`: when `reconnectWindowActive_` is true, iterate `sortedReceivers_` for entry where `stableId == config_.lastDevice` or `instanceName == config_.lastDevice`; if found: set `connectingReceiverIdx_ = i`, save `priorConnectedIdx_ = -1`, call `Connect(sortedReceivers_[i])`, `SetTimer(hwnd_, TIMER_HANDSHAKE_TIMEOUT, 5000, nullptr)`, `KillTimer(hwnd_, TIMER_RECONNECT_WINDOW)`, set `reconnectWindowActive_ = false` (quickstart §Step 9 §HandleReceiversUpdated auto-select; spec §FR-019)
- [x] T032 [US3] Persist stable device ID in `AppController::HandleRaopConnected` in `src/core/AppController.cpp`: after checkmark commit (`connectedReceiverIdx_ = connectingReceiverIdx_`) write `config_.lastDevice = sortedReceivers_[connectedReceiverIdx_].stableId` then call `config_.Save()` (spec §FR-018; R-001; data-model.md §Config)

**Checkpoint**: Select a speaker → close AirBeam → relaunch with same speaker on LAN → auto-
connected within 10 s. Relaunch with speaker offline → after 30 s no auto-connect occurs;
device does NOT auto-connect when it later appears.

---

## Phase 6: User Story 4 — Stale Speaker Removal (Priority: P2)

**Goal**: Devices that stop advertising are removed from the menu within 60 s (via PruneStale)
or immediately on Bonjour remove-event. If the removed device was active, disconnect immediately.

**Independent Test**: Power off a speaker → within 65 s it disappears from the menu without
any user interaction. See quickstart.md §Manual Testing item 8.

**Depends on**: Phase 3 complete (HandleReceiversUpdated Disconnect() path in T015).

- [x] T033 [US4] Audit `ReceiverList.cpp`: confirm `kStaleTimeoutMs = 60000` and `kPruneIntervalMs = 30000` are defined (SC-002 budget: 90 s worst-case); confirm `PruneStale(ULONGLONG nowTicks)` uses `ULONGLONG` subtraction (no DWORD cast) now that `lastSeenTick` is `ULONGLONG` (T009); confirm `PruneStale` posts `WM_RECEIVERS_UPDATED` when entries are removed in `src/discovery/ReceiverList.cpp` (R-007; spec §FR-005; data-model.md §ReceiverList)
- [x] T034 [US4] Verify in `src/discovery/MdnsDiscovery.cpp` that the Bonjour service-removed branch in `BrowseCallback` (the `kDNSServiceFlagsAdd` not-set path) calls `ReceiverList::Remove(instanceName)`, which posts `WM_RECEIVERS_UPDATED`, which triggers `HandleReceiversUpdated` in `AppController`, which in T015 calls `Disconnect()` if the removed device was the active speaker — trace the full path and add any missing link (spec §FR-005a)

**Checkpoint**: Kill a shairport-sync instance → Bonjour remove-event fires immediately →
device vanishes from menu and active stream stops. If no remove-event, 60 s stale timeout
prunes the entry and disconnects.

---

## Phase 7: User Story 5 — AirPlay 2-Only Device Filtering (Priority: P3)

**Goal**: Devices with `pk` TXT field or without `et` containing `"1"` are silently excluded
from the speaker menu — only devices AirBeam can stream to are shown.

**Independent Test**: On a network with a known AirPlay 2-only device (advertises `pk=…`),
confirm it does NOT appear in the menu. See quickstart.md §Manual Testing item 2.

**Depends on**: Phase 2 (T012 `isAirPlay1Compatible` flag set in TxtRecord::Parse, T013 filter
in ReceiverList::Update) — the core filter was implemented in Phase 2. This phase validates
and cleans up any remaining exposure.

- [x] T035 [US5] Confirm no AirPlay 2-specific display code remains in `src/ui/TrayMenu.cpp` (e.g., grayed-out items for non-compatible devices); the contract is that `receivers` passed to `TrayMenu::Show` are already AirPlay1-filtered by `ReceiverList`; remove any such code found and verify the `et`/`pk` filter logic in `TxtRecord.cpp` covers all cases in the contract table (contracts/txt-record-parsing.md §AirPlay 1 Filter Specification — all 7 rows)

**Checkpoint**: `ctest --preset msvc-x64-debug-ci -L unit -R "txt"` TxtRecord filter tests all
pass. Manual: AirPlay 2-only device (with `pk`) is absent from menu; AirPlay 1 device (et
contains "1", no pk) is present.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Build verification, full test suite, and manual acceptance testing.

- [x] T036 [P] Run `cmake --preset msvc-x64-debug-ci && cmake --build --preset msvc-x64-debug-ci` from repo root; confirm zero errors and zero new warnings; fix any compilation issues before proceeding
- [x] T037 Run `ctest --preset msvc-x64-debug-ci -L unit --output-on-failure`; all unit tests must pass including the pre-existing suite plus `test_mdns_txt` (extended), `test_receiver_list` (new), `test_tray_menu` (new)
- [x] T038 Execute the 10-item manual testing checklist in `specs/008-mdns-tray-discovery/quickstart.md` against a live AirPlay 1 receiver (shairport-sync acceptable); record pass/fail for each item
- [x] T039 [P] Resolve all `[TBD]` translation placeholders in `resources/locales/strings_de.rc`, `strings_fr.rc`, `strings_es.rc`, `strings_ja.rc`, `strings_zh-Hans.rc`, `strings_ko.rc` for the 4 new IDS_MENU_* strings (R-010; required before release per project convention). **RELEASE BLOCKER**: No locale `.rc` file may contain `[TBD]` values when this task is marked complete. Add a CI check (or pre-commit hook) that fails if any `.rc` file in `resources/` contains the literal string `[TBD]`.

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1: Setup          → No dependencies. Start immediately.
                            T009, T010 parallel with T001–T008 (different files)
                            T002–T008 require T001 (resource_ids.h defines the IDs)
Phase 2: Foundational   → Requires Phase 1 complete (T009: AirPlayReceiver.h)
                            T011, T012, T013 all parallel (different .cpp files)
Phase 3: US1 (P1)       → Requires Phase 2 complete
                            T019, T020 parallel (different test files)
Phase 4: US2 (P1)       → Requires Phase 3 complete (TrayMenu signature from T017)
                            T028 parallel with T022–T027 (different file)
Phase 5: US3 (P2)       → Requires Phase 4 complete (Connect/HandleRaopConnected flow)
Phase 6: US4 (P2)       → Requires Phase 3 complete (HandleReceiversUpdated T015)
                            Can run in parallel with Phase 5
Phase 7: US5 (P3)       → Requires Phase 2 complete; can run in parallel with Phases 5–6
Phase 8: Polish         → Requires all desired phases complete
```

### User Story Independence

| Story | Depends On | Can Deliver Independently After |
|-------|-----------|--------------------------------|
| US1 (P1) | Phase 2 | Phase 3 complete |
| US2 (P1) | US1 (Phase 3) | Phase 4 complete |
| US3 (P2) | US1 + US2 | Phase 5 complete |
| US4 (P2) | US1 (Phase 3, T015 only) | Phase 6 complete |
| US5 (P3) | Phase 2 | Phase 7 complete |

### Within Each Phase

- T009 (`AirPlayReceiver.h`) before T011 (`MdnsDiscovery.cpp` uses stableId)
- T017 (TrayMenu signature) before T022 (TrayMenu speaker rendering fills in the method body)
- T015 (HandleReceiversUpdated) before T031 (auto-select logic appended to same function)
- T024 (HandleRaopConnected) before T032 (stableId persistence added to same handler)
- T028 (test_tray_menu.cpp creation) before T029 (CMakeLists registration)
- T019 (test_receiver_list.cpp creation) before T021 (CMakeLists registration)

---

## Parallel Execution Examples

### Phase 1 — Maximum Parallelism

```
Parallel group A (can all run simultaneously after T001):
  T002  resources/locales/strings_en.rc
  T003  resources/locales/strings_de.rc
  T004  resources/locales/strings_fr.rc
  T005  resources/locales/strings_es.rc
  T006  resources/locales/strings_ja.rc
  T007  resources/locales/strings_zh-Hans.rc
  T008  resources/locales/strings_ko.rc

Parallel group B (can run simultaneously, independent of group A):
  T009  src/discovery/AirPlayReceiver.h
  T010  src/core/AppController.h
```

### Phase 2 — All Three Parallel

```
T011  src/discovery/MdnsDiscovery.cpp   (stableId, GetTickCount64 fix)
T012  src/discovery/TxtRecord.cpp       (an field, displayName)
T013  src/discovery/ReceiverList.cpp    (AirPlay1 filter guard)
```

### Phase 3 — Test Writing Parallel With Later Implementation

```
Start T014 first (BrowseCallback pipeline — most complex task in this phase)
T019 and T020 can be written in parallel while T015–T018 are in progress
```

### Phases 5, 6, 7 — Parallel After Phase 4

```
Phase 5 (US3): T030 → T031 → T032   (sequential; all AppController.cpp)
Phase 6 (US4): T033 → T034           (sequential; different files)
Phase 7 (US5): T035                  (independent of phases 5 & 6)
```

---

## Implementation Strategy

### MVP First (US1 + US2 — Both P1)

1. Complete **Phase 1** (Setup — resource IDs + headers)
2. Complete **Phase 2** (Foundational — discovery pipeline)
3. Complete **Phase 3** (US1 — auto-discovery, searching placeholder, Bonjour-missing)
4. **STOP and VALIDATE**: Speakers appear in menu within 10 s. Tests pass. →
5. Complete **Phase 4** (US2 — speaker selection, pessimistic checkmark, 3-vs-4 menu)
6. **STOP and VALIDATE**: Click-to-connect flow works end-to-end. Tests pass. →
7. **Deploy/Demo**: Both P1 stories delivered — full core value demonstrated.

### Incremental Delivery

```
Phases 1–2 → Foundation ready (no user-visible change)
Phase 3    → US1 done: speakers appear in menu (first visible value) ← Demo here
Phase 4    → US2 done: click to switch speakers, checkmark flow ← MVP complete
Phase 5    → US3 done: auto-reconnect on restart
Phases 6+7 → US4+US5 done: stale removal + AirPlay2 filter hardened ← Feature complete
```

### Critical Path

The longest sequential chain is:
`T001 → T009 → T011/T012/T013 → T014 → T015–T018 → T022–T027 → T030–T032`

Parallelize locale files (T002–T008), both header changes (T009/T010), all foundational .cpp
files (T011/T012/T013), and test authoring (T019/T020/T028) to minimize wall-clock time.

---

## Notes

- **Threading invariant**: All speaker-list mutations flow via `PostMessage` → UI thread.
  Never call `ReceiverList` mutating methods from `AppController` handlers; only `Snapshot()`.
- **`TIMER_HANDSHAKE_TIMEOUT` invariant**: `connectingReceiverIdx_ >= 0` iff the timer is
  active. Always set and kill together. See data-model.md §SpeakerMenuState state transitions.
- **`TrayMenu::Show` always builds fresh**: No menu-handle caching. `DestroyMenu(hMenu)` at
  end of each call cascades to child submenus (Win32 guarantee — R-004).
- **stableId vs macAddress**: `stableId` (wstring, from instance name) is the canonical
  persisted ID. The existing `macAddress` field (string, from TXT) is now superseded for
  persistence purposes — do not write `macAddress` to `config_.lastDevice` (R-001).
- **[P] = different files, no incomplete dependencies**: Tasks marked [P] within the same
  phase may be assigned to different agents/developers and worked simultaneously.
- **[TBD] translations**: Acceptable in a first implementation PR per project convention;
  must be resolved before release (T039).
