# Research: mDNS Discovery and Tray Speaker Menu

**Branch**: `008-mdns-tray-discovery`  
**Phase**: 0 — Research  
**Status**: Complete (all NEEDS CLARIFICATION resolved)

---

## R-001 — Stable Device Identifier from mDNS Instance Name

**Decision**: Extract the MAC address prefix that precedes `@` in the `_raop._tcp` service
instance name (e.g., `"AA:BB:CC:DD:EE:FF@Living Room"` → stable ID = `"AA:BB:CC:DD:EE:FF"`).

**Rationale**: The RAOP protocol mandates that the service instance name begin with the
device's MAC address in upper-case hex-colon notation followed by `@`. This portion is
persistent across reboots and network changes, making it the correct stable identifier for
`Config::lastDevice`. The existing `AppController::HandleRaopConnected` already writes
`connectedReceiver_.macAddress` to `config_.lastDevice`, but `macAddress` is currently
populated from TXT fields rather than the instance name — this is a bug. Fix: parse the MAC
from instance name in `BrowseCallback` via a new helper `DeviceIdFromInstance()`.

**Alternatives considered**:
- Full instance name as ID — rejected because the human-readable suffix can change if the user
  renames their device.
- TXT `am` or `vs` fields as ID — rejected because these are model/version strings, not
  unique per device.

**Implementation note**: `DeviceIdFromInstance(instanceName)` returns the substring before
the first `@`, uppercased. If no `@` is found, return the full instance name unchanged.

---

## R-002 — Display Name Construction (TXT `an` + `am`)

**Decision**: Primary display name = TXT `an` field. If TXT `am` (model) is also present,
append ` (am_value)` in parentheses. Truncate the full composed string at 40 characters
(append `…` ellipsis if truncated). If `an` is absent, fall back to the human-readable
portion of the instance name (the substring after `@`, already extracted in `BrowseCallback`
via `DisplayNameFromInstance`).

**Rationale**: The `an` field carries the user-visible device name set by the owner (e.g.
"Living Room"). Appending the model disambiguates devices with identical friendly names
(e.g., two "AirPort Express" units). The 40-char limit prevents tray-menu overflow. This
logic must live in `TxtRecord::Parse` so that it can update `AirPlayReceiver::displayName`
in the same pass that populates `isAirPlay1Compatible`.

**Alternatives considered**:
- Hostname as display name — rejected; `.local` hostnames are not user-visible labels.
- Instance name after `@` only — retained as fallback; sufficient when `an` is absent.
- 32-char limit — spec explicitly set 40 chars; 32 was considered too aggressive for common
  device names like "Living Room HomePod mini (AudioAccessory1,1)".

**Implementation note**: In `TxtRecord::Parse`, add `else if (key == "an")` to capture the
field. After the parse loop, build `displayName = an + " (" + am + ")"` (when both present),
then apply truncation. The truncation function: if `wcslen(composed) > 40`, copy first 39
wide chars and append `L"…"` (U+2026).

---

## R-003 — Alphabetical Sort Strategy

**Decision**: Use `std::sort` with a comparator on `displayName` (wide-string lexicographic,
`std::wstring::operator<`). Sort is applied once per `HandleReceiversUpdated` call when the
sorted snapshot is rebuilt on the UI thread.

**Rationale**: Wide-string comparison in Windows is byte-ordered for BMP characters, which
produces correct A→Z ordering for the Latin-alphabet device names common in practice. For
non-Latin names (Japanese HomePods, Chinese AirPort Expresses), byte-ordered sort still
produces a stable, repeatable order — fully consistent with the spec requirement "stable
across restarts". Locale-aware `CompareStringW` is unnecessary overhead for ≤ 100 items and
was not required by the spec.

**Alternatives considered**:
- `CompareStringW(LOCALE_USER_DEFAULT, ...)` — more correct for non-Latin scripts but adds
  complexity and the spec only requires A→Z (alphabetical); deferred.
- Sort at ReceiverList level (in `Update`) — rejected because it couples sorting logic to the
  discovery thread; sorting on the UI thread (in `HandleReceiversUpdated`) keeps Thread 2
  free of presentation concerns.

---

## R-004 — Win32 Submenu Construction for 4+ Speakers

**Decision**: When `sortedReceivers_.size() >= 4`, create a child menu via `CreatePopupMenu()`,
populate it with speaker items, then attach it to the root popup with
`AppendMenuW(hMenu, MF_POPUP | MF_STRING, (UINT_PTR)hSubMenu, speakersLabel)`. The child
menu is NOT separately destroyed; `DestroyMenu(hMenu)` cascades to child submenus on Win32.

**Rationale**: Win32 documents that destroying a menu also destroys all child popup menus.
The `TrayMenu::Show` already calls `DestroyMenu(hMenu)` at the end of each call, so no
additional cleanup is required. Command IDs (IDM_DEVICE_BASE + i) are the same whether
speakers are inline or in a submenu — `HandleCommand` does not need to distinguish.

**Alternatives considered**:
- Separate DestroyMenu for hSubMenu — technically not needed per Win32 docs; would require
  saving hSubMenu across the TrackPopupMenu call.
- AppendMenuW with MF_OWNERDRAW — rejected; overly complex for no benefit.

**Implementation note**: `TrayMenu::Show` receives a new `bool bonjourMissing` parameter
(added before `receivers`) and an `int connectingReceiverIdx` parameter. Signature change:

```cpp
UINT Show(HWND hwnd, const Config& config, bool sparkleAvailable,
          bool bonjourMissing,
          const std::vector<AirPlayReceiver>& receivers,
          int connectedReceiverIdx,
          int connectingReceiverIdx);
```

---

## R-005 — Pessimistic Checkmark + 5-Second Handshake Timeout

**Decision**: On speaker click, set `connectingReceiverIdx_` and start a new timer
`TIMER_HANDSHAKE_TIMEOUT = 3` at 5000 ms. `WM_RAOP_CONNECTED` commits the checkmark
(`connectedReceiverIdx_ = connectingReceiverIdx_`) and kills the timer.
`WM_RAOP_FAILED` OR timer fire both revert: restore `priorConnectedIdx_` into
`connectedReceiverIdx_`, set `connectingReceiverIdx_ = -1`, kill the timer.

**Rationale**: The spec (FR-016 through FR-016c) requires that the checkmark is only committed
on confirmed handshake success. The existing `AppController::Connect()` sets `isConnected_ =
true` immediately — this must be changed to a "pending" state. The existing
`TIMER_RAOP_RETRY` handles retry after confirmed stream failure; the new
`TIMER_HANDSHAKE_TIMEOUT` handles timeout during the initial pessimistic connection attempt.
These are distinct timer IDs to avoid interference.

**Cancel-and-redirect** (FR-016c): when the user clicks Speaker B while Speaker A's handshake
is in progress, `raopSession_->Stop()` is called and `raopSession_.reset()` before calling
`Connect(speakerB)`. This immediately terminates the RTSP socket. No WM_RAOP_FAILED is
expected after explicit cancellation — the timer is killed proactively.

**Alternatives considered**:
- Optimistic checkmark (move immediately, revert on failure) — spec explicitly chose
  pessimistic; rejected.
- Re-using TIMER_RAOP_RETRY for both purposes — rejected; the retry counter would be
  corrupted if a handshake timeout triggered while the retry loop was active.

---

## R-006 — 30-Second Auto-Select Window Fix

**Decision**: Change `SetTimer(hwnd_, TIMER_RECONNECT_WINDOW, 5000, nullptr)` in
`AppController::Start` to 30 000 ms to match the spec (FR-019, SC-006).

**Rationale**: The existing code uses 5 000 ms — a hard-coded stub value from a prior
implementation phase. The spec is unambiguous: "30 seconds". No spec-change is needed; this
is a pure bug fix.

**Alternatives considered**: None. The spec is locked.

---

## R-007 — `lastSeenTick` Type: DWORD → ULONGLONG

**Decision**: Change `AirPlayReceiver::lastSeenTick` from `DWORD` to `ULONGLONG` and populate
it with `GetTickCount64()` in `AddrInfoCallback`.

**Rationale**: `GetTickCount()` (DWORD) wraps after ~49.7 days. The existing
`ReceiverList::PruneStale(ULONGLONG nowTicks)` casts `lastSeenTick` to `ULONGLONG` via
`static_cast`, which produces a subtraction underflow after the wrap, causing ALL entries to
appear stale and be evicted simultaneously. Changing to `ULONGLONG` eliminates the mismatch.

**Alternatives considered**:
- Detect wrap in PruneStale — adds complexity; changing the field type is cleaner.

---

## R-008 — AirPlay 1 Filter Location

**Decision**: Filter in `ReceiverList::Update()`: only `push_back` (or replace) when
`receiver.isAirPlay1Compatible == true`. Non-AirPlay-1 receivers are silently discarded.
The existing grayed-out AirPlay2 display in `TrayMenu.cpp` is removed.

**Rationale**: The spec (FR-003) says the application "MUST include a discovered device in
the speaker list ONLY when" the filter passes. Moving the filter to `ReceiverList` is the
cleanest enforcement point. `isAirPlay1Compatible` is set by `TxtRecord::Parse` before
`Update` is called (it is called from `AddrInfoCallback`, the last step in the pipeline).

**Alternatives considered**:
- Filter in TrayMenu — rejected; ReceiverList would contain junk entries that are never
  shown; PruneStale would waste time on them.
- Filter in TxtRecord — rejected; TxtRecord only sets a flag, not a list entry.

---

## R-009 — HandleReceiversUpdated Completeness

**Decision**: `AppController::HandleReceiversUpdated` must do all of the following on every
call, not just during the auto-select window:

1. Take AirPlay1-filtered snapshot from `ReceiverList`.
2. Sort alphabetically by `displayName`.
3. Store as `sortedReceivers_` (used by `ShowTrayMenu` and `HandleCommand`).
4. Re-derive `connectedReceiverIdx_` and `connectingReceiverIdx_` by finding matching
   `instanceName` in the new sorted list (indices shift after sort).
5. If the connected receiver's `instanceName` is NOT in the new list → call `Disconnect()`.
6. If `reconnectWindowActive_` → check for auto-select match (existing logic, now inside
   the per-update path).

**Rationale**: The current implementation only runs during the reconnect window. Steps 3–5
are missing entirely, which means the menu never reflects discovery changes during a session.

---

## R-010 — New Resource IDs (Constitution VIII compliance)

**Decision**: Add four new string IDs to `resources/resource_ids.h` in the 1028–1031 range:

| ID | Value | English string |
|----|-------|----------------|
| `IDS_MENU_SEARCHING` | 1028 | `"Searching for speakers\x2026"` |
| `IDS_MENU_BONJOUR_MISSING` | 1029 | `"Install Bonjour to discover speakers"` |
| `IDS_MENU_CONNECTING` | 1030 | `" \x2014 Connecting\x2026"` (suffix appended to speaker label) |
| `IDS_MENU_SPEAKERS` | 1031 | `"Speakers"` (submenu header) |

All 7 locale files receive these four entries. The English strings are the canonical source.
Translated strings will follow the same format; placeholder translations marked `[TBD]` in
non-English locales are acceptable for the first implementation PR and must be resolved before
release (existing convention in the project).

---

## R-011 — AppController State Additions

**Decision**: Add three new private members to `AppController`:

```cpp
// Speaker menu state (all accessed only from Thread 1 / UI thread)
std::vector<AirPlayReceiver> sortedReceivers_;   // alphabetical, AirPlay1 only
int  connectingReceiverIdx_    = -1;             // speaker showing "Connecting…"
int  priorConnectedIdx_        = -1;             // snapshot before pessimistic attempt
bool bonjourMissing_           = false;          // drives IDS_MENU_BONJOUR_MISSING

static constexpr UINT TIMER_HANDSHAKE_TIMEOUT = 3;  // 5-second pessimistic timeout
```

`Config::lastDevice` remains `std::wstring`; it stores the MAC-based stable ID (already the
intent — the bug is in how `macAddress` is populated, fixed by R-001).

**Alternatives considered**:
- A separate `SpeakerMenuState` struct — adds indirection without benefit; all state is
  owned by `AppController` already.
