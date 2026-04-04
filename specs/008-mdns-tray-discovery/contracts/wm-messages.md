# Contract: Win32 Message Protocol

**Branch**: `008-mdns-tray-discovery`  
**File**: `src/core/Messages.h`

This document defines the Win32 message protocol for cross-thread communication
relevant to this feature. All messages are posted via `PostMessage` (asynchronous).

---

## Existing Messages (unchanged)

| Message constant | `WM_APP +` | Sender → Receiver | Payload |
|-----------------|-----------|-------------------|---------|
| `WM_TRAY_CALLBACK` | +1 | Shell_NotifyIcon → `WndProc` | `lParam` = mouse/notification event |
| `WM_TRAY_POPUP_MENU` | +2 | Internal | — |
| `WM_RAOP_CONNECTED` | +3 | `RaopSession` (Thread 5) → `AppController` (Thread 1) | `lParam` = reserved |
| `WM_RAOP_FAILED` | +4 | `RaopSession` (Thread 5) → `AppController` (Thread 1) | `lParam` = reserved |
| `WM_RECEIVERS_UPDATED` | +5 | `ReceiverList` (any thread) → `AppController` (Thread 1) | none |
| `WM_DEFAULT_DEVICE_CHANGED` | +6 | Audio subsystem → `AppController` | none |
| `WM_BONJOUR_MISSING` | +7 | `AppController::Start` → itself | none |
| `WM_TEARDOWN_COMPLETE` | +8 | Internal shutdown protocol | none |
| `WM_UPDATE_REJECTED` | +9 | WinSparkle integration → `AppController` | none |

---

## Message Semantics for This Feature

### `WM_RECEIVERS_UPDATED` (Thread 2 → Thread 1)

**Posted by**: `ReceiverList::NotifyUpdated()` after every `Update()`, `Remove()`, or
`PruneStale()` call that modifies the list.

**Handled by**: `AppController::HandleReceiversUpdated()`

**Contract**:
- Posted from Thread 2 (Bonjour callback loop) or Thread 1 (no constraint).
- `wParam = 0`, `lParam = 0`.
- Handler MUST NOT call `ReceiverList` methods that lock `cs_` from within the handler
  while still inside the CS (safe because `PostMessage` is asynchronous — the handler
  runs on a separate UI-thread message pump iteration).
- Handler calls `receiverList_->Snapshot()` to obtain an AirPlay1-filtered, unordered copy,
  then sorts it into `sortedReceivers_`.
- Handler re-derives `connectedReceiverIdx_` and `connectingReceiverIdx_` from
  `instanceName` matches in the new sorted list.
- Handler checks if the connected receiver has been removed → calls `Disconnect()`.
- Handler checks auto-select window if `reconnectWindowActive_`.

**Frequency**: May arrive in rapid bursts during device churn. Each call rebuilds
`sortedReceivers_` and re-derives indices. No coalescing needed — each rebuild is
idempotent and O(n log n) for ≤ 100 items.

---

### `WM_RAOP_CONNECTED` (Thread 5 → Thread 1)

**Posted by**: `RaopSession` after successful RTSP ANNOUNCE/SETUP/RECORD handshake.

**Handled by**: `AppController::HandleRaopConnected(LPARAM)`

**Contract** (changes for this feature):
- Before this feature: `isConnected_` was set in `Connect()`; checkmark was immediate.
- **After this feature**: `isConnected_` is set here (not in `Connect()`).
- Handler MUST: kill `TIMER_HANDSHAKE_TIMEOUT`, set `connectedReceiverIdx_ = connectingReceiverIdx_`,
  set `connectingReceiverIdx_ = -1`, set `isConnected_ = true`, start WASAPI + ALAC threads.
- Handler MUST persist `stableId` to `config_.lastDevice`.

---

### `WM_RAOP_FAILED` (Thread 5 → Thread 1)

**Posted by**: `RaopSession` on any RTSP error or TCP disconnect.

**Handled by**: `AppController::HandleRaopFailed(LPARAM)`

**Contract** (changes for this feature):
Two distinct cases must be handled:

1. **Pessimistic timeout path** (`connectingReceiverIdx_ >= 0`): initial connection attempt
   failed or timed out.
   - Kill `TIMER_HANDSHAKE_TIMEOUT`.
   - Restore `connectedReceiverIdx_ = priorConnectedIdx_`.
   - Set `connectingReceiverIdx_ = -1`.
   - Do NOT show a balloon (silent revert per spec §FR-016b).

2. **Mid-stream failure path** (`isConnected_ == true`, `wasStreaming_ == true`):
   unexpected disconnect during an active stream.
   - Existing retry logic (3 retries with exponential backoff) applies.
   - Balloon notification shown on first failure.

---

### `WM_BONJOUR_MISSING` (Thread 1 → Thread 1)

**Posted by**: `AppController::Start` when `BonjourLoader::Load()` returns false.

**Handled by**: `AppController::HandleBonjourMissing()`

**Contract** (change for this feature):
- Handler MUST set `bonjourMissing_ = true` in addition to the existing balloon notification.
- `bonjourMissing_` drives `IDS_MENU_BONJOUR_MISSING` placeholder in `TrayMenu::Show`.

---

## Timer IDs

| Constant | ID | Duration | Purpose |
|----------|----|----------|---------|
| `TIMER_RECONNECT_WINDOW` | 1 | **30 000 ms** (was 5 000 ms — bug fix) | Auto-select window from startup |
| `TIMER_RAOP_RETRY` | 2 | 1000 / 2000 / 4000 ms (exponential) | Retry after mid-stream failure |
| `TIMER_HANDSHAKE_TIMEOUT` | 3 | **5 000 ms** (new) | Pessimistic connection timeout |
