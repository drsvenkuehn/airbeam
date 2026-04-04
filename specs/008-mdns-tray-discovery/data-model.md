# Data Model: mDNS Discovery and Tray Speaker Menu

**Branch**: `008-mdns-tray-discovery`  
**Phase**: 1 — Design  
**Status**: Complete

---

## Entities

### 1. `AirPlayReceiver` (modified — `src/discovery/AirPlayReceiver.h`)

Describes one AirPlay receiver discovered via mDNS (`_raop._tcp`).

| Field | Type | Description | Change |
|-------|------|-------------|--------|
| `instanceName` | `std::wstring` | mDNS service instance name (`"MACADDR@FriendlyName"`) | existing |
| `stableId` | `std::wstring` | Stable device identifier — MAC prefix before `@` (e.g. `"AA:BB:CC:DD:EE:FF"`). Fallback: full `instanceName` if no `@` present. | **NEW** |
| `displayName` | `std::wstring` | Human-readable label shown in menu: `an` TXT field + ` (am)` if model present, truncated to 40 chars. Fallback: instance name after `@`. | existing, **fully populated** |
| `hostName` | `std::wstring` | mDNS hostname (e.g. `"AppleTV.local."`) | existing |
| `ipAddress` | `std::string` | Dotted-decimal IPv4 address (populated after `GetAddrInfo` callback) | existing |
| `port` | `uint16_t` | TCP port for RTSP control (default 5000) | existing |
| `isAirPlay1Compatible` | `bool` | `true` iff `et` contains `"1"` AND `pk` is absent | existing |
| `deviceModel` | `std::string` | TXT `am` field (e.g. `"AppleTV5,3"`) | existing |
| `protocolVersion` | `std::string` | TXT `vs` field (e.g. `"130.14"`) | existing |
| `lastSeenTick` | `ULONGLONG` | `GetTickCount64()` at last update (was `DWORD`, **type fix**) | **MODIFIED** |
| `macAddress` | `std::string` | **Removed from field doc** — superseded by `stableId` (wstring). Deprecated in v1.0 — `stableId` is the canonical persisted identifier. `macAddress` is retained only for debug logging; it MUST NOT be used for device matching or persistence. | existing |

**Validation rules**:
- `isAirPlay1Compatible` must be `true` before any `AirPlayReceiver` is stored in `ReceiverList`
  (enforced by `ReceiverList::Update` filter — R-008).
- `stableId` must be non-empty; populated in `MdnsDiscovery::BrowseCallback` before
  `pendingResolve_` is seeded.
- `displayName` must be non-empty; populated from `an` TXT field in `TxtRecord::Parse` or
  fallback to instance name suffix.
- `lastSeenTick` must be set by `GetTickCount64()` in `AddrInfoCallback` just before
  `ReceiverList::Update` is called.

**Invariants**:
- `port` is host-byte-order (converted from network-byte-order in `ResolveCallback`).
- `ipAddress` is dotted-decimal IPv4; IPv6 is not supported in v1.0.

---

### 2. `ReceiverList` (modified — `src/discovery/ReceiverList.h / .cpp`)

Thread-safe collection of **AirPlay 1-compatible** receivers. Backed by a `CRITICAL_SECTION`.

| Method | Behaviour | Change |
|--------|-----------|--------|
| `Update(const AirPlayReceiver&)` | Upsert by `instanceName`. **Now silently discards** entries where `isAirPlay1Compatible == false`. Posts `WM_RECEIVERS_UPDATED` on change. | **MODIFIED** |
| `Remove(const std::wstring&)` | Remove entry by `instanceName`. Posts `WM_RECEIVERS_UPDATED` if removed. | existing |
| `PruneStale(ULONGLONG nowTicks)` | Evict entries older than 60 000 ms. Posts `WM_RECEIVERS_UPDATED` if any removed. | existing (correct with ULONGLONG fix) |
| `ForEach(fn)` | Read-only iteration under lock. | existing |
| `Snapshot()` | Lock-free copy. Returns only AirPlay1-compatible receivers (enforced by Update filter). | existing |

**Stale timeout**: 60 000 ms (60 seconds, spec §FR-005). `PruneStale` is called every
`kPruneIntervalMs = 30 000` ms from Thread 2. Maximum observable staleness before eviction:
60 000 + 30 000 = 90 000 ms — worst-case ≤ 90 s (SC-002).

---

### 3. `TxtRecord` parsing contract (modified — `src/discovery/TxtRecord.cpp`)

Parses a raw DNS-SD TXT record buffer and fills `AirPlayReceiver` fields.

| TXT Key | Action | Change |
|---------|--------|--------|
| `et` | Set `etHas1 = true` if comma-separated token `"1"` is present | existing |
| `pk` | Set `pkPresent = true` | existing |
| `am` | Set `out.deviceModel` | existing |
| `vs` | Set `out.protocolVersion` | existing |
| `an` | Set `friendlyName` local var; **NEW** | **NEW** |
| *(post-loop)* | Set `out.isAirPlay1Compatible = etHas1 && !pkPresent` | existing |
| *(post-loop)* | Build `out.displayName` from `an` + `am`; apply 40-char truncation | **NEW** |

**Display name construction algorithm**:
```
if an is non-empty:
    composed = wide(an)
    if am is non-empty:
        composed += L" (" + wide(am) + L")"
else:
    composed = out.displayName   // already set from instance name in BrowseCallback
if composed.length() > 40:
    composed = composed.substr(0, 39) + L"\u2026"
out.displayName = composed
```

**Note**: `TxtRecord::Parse` receives an `AirPlayReceiver&` that already has `displayName`
pre-populated from `DisplayNameFromInstance` (set in `BrowseCallback`). The parse function
overwrites `displayName` only when `an` is present.

---

### 4. `SpeakerMenuState` (new state within `AppController`)

Maintained exclusively on Thread 1 (UI thread). No synchronization required.

| Member | Type | Description |
|--------|------|-------------|
| `sortedReceivers_` | `std::vector<AirPlayReceiver>` | Alphabetically sorted, AirPlay1-only snapshot. Rebuilt on each `WM_RECEIVERS_UPDATED`. |
| `connectedReceiverIdx_` | `int` | Index into `sortedReceivers_` of the committed active speaker (checkmark). `-1` = none. |
| `connectingReceiverIdx_` | `int` | Index of speaker showing `"— Connecting…"` label. `-1` = none. |
| `priorConnectedIdx_` | `int` | Saved `connectedReceiverIdx_` before a pessimistic attempt; restored on timeout/failure. |
| `bonjourMissing_` | `bool` | Set in `HandleBonjourMissing`. Drives `IDS_MENU_BONJOUR_MISSING` placeholder. |

**State transitions**:

```
IDLE (connectedIdx=-1, connectingIdx=-1)
  │
  ├─[user clicks speaker i]──────────────────────────────────┐
  │   priorConnectedIdx_ = connectedIdx_                     │
  │   connectingIdx_ = i                                     │
  │   Connect(sortedReceivers_[i])                           │
  │   SetTimer(TIMER_HANDSHAKE_TIMEOUT, 5000)                │
  │                                                           ▼
  │                                          PENDING (connectingIdx=i)
  │                                             │
  │                                  ┌──────────┴──────────────────────────┐
  │                       WM_RAOP_CONNECTED              WM_RAOP_FAILED or
  │                       (handshake OK)                 TIMER_HANDSHAKE_TIMEOUT
  │                             │                                │
  │                  connectedIdx_ = connectingIdx_    connectedIdx_ = priorConnectedIdx_
  │                  connectingIdx_ = -1               connectingIdx_ = -1
  │                  KillTimer(HANDSHAKE_TIMEOUT)       KillTimer(HANDSHAKE_TIMEOUT)
  │                             │                                │
  │                       STREAMING                         IDLE / prior state restored
  │
  ├─[stale remove / Bonjour remove-event for connected speaker]
  │   Disconnect()
  │   connectedIdx_ = -1
  │   → IDLE
  │
  └─[user clicks speaker j while connectingIdx_=i]
      KillTimer(TIMER_HANDSHAKE_TIMEOUT)
      raopSession_->Stop()  ← cancels in-flight RTSP
      priorConnectedIdx_ = connectedIdx_  (unchanged)
      connectingIdx_ = j
      Connect(sortedReceivers_[j])
      SetTimer(TIMER_HANDSHAKE_TIMEOUT, 5000)
      → PENDING (connectingIdx=j)
```

---

### 5. `Config` (existing — `src/core/Config.h`)

No struct changes. Existing `lastDevice` wstring stores the stable device ID (MAC prefix
from instance name, e.g. `L"AA:BB:CC:DD:EE:FF"`). The bug where `macAddress` was populated
from TXT fields is fixed by R-001 (populate `stableId` from instance name in `BrowseCallback`
and write `config_.lastDevice = connectedReceiver_.stableId` in `HandleRaopConnected`).

---

### 6. Menu Command ID Space (existing — `src/core/Commands.h`)

No changes required. The existing ID ranges are sufficient:

| Range | Purpose |
|-------|---------|
| 1000–1999 | Static menu items (IDM_QUIT, IDM_CHECK_UPDATES, …) |
| 2000–2099 | Dynamic speaker items: `IDM_DEVICE_BASE + i` where `i` is index into `sortedReceivers_` |

`IDM_DEVICE_MAX_COUNT = 100` accommodates the maximum practical speaker count.

---

## Relationships

```
ReceiverList  ──── PostMessage(WM_RECEIVERS_UPDATED) ────► AppController (Thread 1)
    ▲                                                              │
    │ Update()/Remove()/PruneStale()                 HandleReceiversUpdated()
    │                                                              │
MdnsDiscovery                                         sorts → sortedReceivers_
  (Thread 2)                                          re-derives connectedIdx_
    │                                                      │
    ├─ BrowseCallback: seed AirPlayReceiver               │
    ├─ ResolveCallback: TxtRecord::Parse → displayName,    │
    │                   isAirPlay1Compatible               │
    └─ AddrInfoCallback: ipAddress, lastSeenTick           │
                         ReceiverList::Update()            │
                                                    ShowTrayMenu():
                                                      TrayMenu::Show(
                                                        sortedReceivers_,
                                                        connectedIdx_,
                                                        connectingIdx_,
                                                        bonjourMissing_)
```
