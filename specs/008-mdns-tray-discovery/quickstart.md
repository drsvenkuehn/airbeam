# Quickstart: mDNS Discovery and Tray Speaker Menu

**Branch**: `008-mdns-tray-discovery`  
**Audience**: Developer implementing the feature from `tasks.md`

---

## Prerequisites

- Build environment from `CONTRIBUTING.md` is working (MSVC 2022 v143, CMake 3.20+, Ninja).
- Bonjour SDK for Windows is installed on the dev machine (for manual testing).
- AirPlay 1 receiver on the same LAN (Apple TV gen ≤ 4, AirPort Express, or shairport-sync
  in Docker/WSL2 with `_raop._tcp` advertising).
- All existing unit tests pass: `ctest --preset msvc-x64-debug-ci -L unit`.

---

## Implementation Order

Follow this sequence to avoid broken intermediate states:

### Step 1 — `AirPlayReceiver.h`: Add `stableId`, fix `lastSeenTick`

```cpp
// Change:
DWORD lastSeenTick = 0;
// To:
ULONGLONG lastSeenTick = 0;

// Add new field:
std::wstring stableId;   // MAC prefix from instance name (e.g. L"AA:BB:CC:DD:EE:FF")
```

### Step 2 — `MdnsDiscovery.cpp`: Add `DeviceIdFromInstance`, fix `lastSeenTick`

Add to the anonymous namespace (alongside `DisplayNameFromInstance`):

```cpp
/// Extract stable device ID: the MAC address prefix before '@' in the instance name.
std::wstring DeviceIdFromInstance(const std::wstring& instanceName)
{
    const auto at = instanceName.find(L'@');
    return (at != std::wstring::npos) ? instanceName.substr(0, at) : instanceName;
}
```

In `BrowseCallback`, after seeding `pendingResolve_.receiver`:
```cpp
self->pendingResolve_.receiver.stableId = DeviceIdFromInstance(instanceName);
```

In `AddrInfoCallback`, fix tick type:
```cpp
r.lastSeenTick = GetTickCount64();   // was GetTickCount()
```

### Step 3 — `TxtRecord.cpp`: Add `an` field parsing + display name construction

In the parse loop, add:
```cpp
else if (key == "an")
{
    friendlyName = value;   // std::string, declared before loop
}
```

After the parse loop, before setting `isAirPlay1Compatible`:
```cpp
// Build displayName from an + am
if (!friendlyName.empty()) {
    std::wstring composed = Utf8ToWide(friendlyName.c_str());
    if (!out.deviceModel.empty()) {
        composed += L" (" + Utf8ToWide(out.deviceModel.c_str()) + L")";
    }
    constexpr std::size_t kMaxLen = 40;
    if (composed.length() > kMaxLen) {
        composed = composed.substr(0, kMaxLen - 1) + L"\u2026";
    }
    out.displayName = std::move(composed);
}
// If friendlyName is empty: out.displayName retains the instance-name fallback set by BrowseCallback.
```

### Step 4 — `ReceiverList.cpp`: AirPlay 1 filter in `Update()`

```cpp
void ReceiverList::Update(const AirPlayReceiver& receiver)
{
    if (!receiver.isAirPlay1Compatible) return;  // ← Add this guard
    // ... rest unchanged
}
```

### Step 5 — `resource_ids.h` + all 7 locale `.rc` files

Add to `resources/resource_ids.h`:
```cpp
#define IDS_MENU_SEARCHING       1028
#define IDS_MENU_BONJOUR_MISSING 1029
#define IDS_MENU_CONNECTING      1030
#define IDS_MENU_SPEAKERS        1031
```

Add to each locale file (translate the values; English shown):
```rc
IDS_MENU_SEARCHING       "Searching for speakers\x2026"
IDS_MENU_BONJOUR_MISSING "Install Bonjour to discover speakers"
IDS_MENU_CONNECTING      " \x2014 Connecting\x2026"
IDS_MENU_SPEAKERS        "Speakers"
```

### Step 6 — `TrayMenu.h`: Update `Show()` signature

```cpp
// Replace the two-overload design with one canonical signature:
UINT Show(HWND hwnd, const Config& config, bool sparkleAvailable,
          bool bonjourMissing,
          const std::vector<AirPlayReceiver>& receivers,
          int connectedReceiverIdx,
          int connectingReceiverIdx);
```

### Step 7 — `TrayMenu.cpp`: Rewrite the receiver section

Key logic (pseudocode):
```
if bonjourMissing:
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, Load(IDS_MENU_BONJOUR_MISSING))
elif receivers.empty():
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, Load(IDS_MENU_SEARCHING))
else:
    targetMenu = (receivers.size() >= 4) ? CreatePopupMenu() : hMenu

    for i, r in enumerate(receivers):
        label = r.displayName
        if i == connectingReceiverIdx:
            label += Load(IDS_MENU_CONNECTING)
        flags = MF_STRING
        if i == connectedReceiverIdx:
            flags |= MF_CHECKED
        AppendMenuW(targetMenu, flags, IDM_DEVICE_BASE + i, label)

    if receivers.size() >= 4:
        AppendMenuW(hMenu, MF_POPUP | MF_STRING, (UINT_PTR)targetMenu,
                    Load(IDS_MENU_SPEAKERS))
```

### Step 8 — `AppController.h`: Add new members + timer constant

```cpp
// New members (Thread 1 only):
std::vector<AirPlayReceiver> sortedReceivers_;
int  connectingReceiverIdx_    = -1;
int  priorConnectedIdx_        = -1;
bool bonjourMissing_           = false;

static constexpr UINT TIMER_HANDSHAKE_TIMEOUT = 3;
```

Fix the timer value in `AppController.cpp`:
```cpp
// In Start(), change 5000 → 30000:
SetTimer(hwnd_, TIMER_RECONNECT_WINDOW, 30000, nullptr);
```

### Step 9 — `AppController.cpp`: Rewrite key handlers

**`HandleReceiversUpdated`** (full rewrite):
```cpp
void AppController::HandleReceiversUpdated() {
    // 1. Take sorted snapshot
    auto raw = receiverList_ ? receiverList_->Snapshot() : std::vector<AirPlayReceiver>{};
    std::sort(raw.begin(), raw.end(),
              [](const auto& a, const auto& b){ return a.displayName < b.displayName; });
    sortedReceivers_ = std::move(raw);

    // 2. Re-derive index for connected receiver
    connectedReceiverIdx_ = -1;
    if (isConnected_) {
        for (int i = 0; i < (int)sortedReceivers_.size(); ++i) {
            if (sortedReceivers_[i].instanceName == connectedReceiver_.instanceName) {
                connectedReceiverIdx_ = i;
                break;
            }
        }
        // Connected receiver removed → disconnect immediately
        if (connectedReceiverIdx_ < 0) {
            Disconnect();
        }
    }

    // 3. Re-derive index for connecting receiver
    if (connectingReceiverIdx_ >= 0) {
        // find by stored connectedReceiver_ pending name
        // (stored separately as connectingReceiver_ — add field or search by index)
    }

    // 4. Auto-select window check
    if (reconnectWindowActive_) {
        for (int i = 0; i < (int)sortedReceivers_.size(); ++i) {
            const auto& r = sortedReceivers_[i];
            if (r.stableId == config_.lastDevice || r.instanceName == config_.lastDevice) {
                connectingReceiverIdx_ = i;
                priorConnectedIdx_ = -1;
                Connect(r, i);
                SetTimer(hwnd_, TIMER_HANDSHAKE_TIMEOUT, 5000, nullptr);
                KillTimer(hwnd_, TIMER_RECONNECT_WINDOW);
                reconnectWindowActive_ = false;
                break;
            }
        }
    }
}
```

**`HandleBonjourMissing`**: add `bonjourMissing_ = true;`.

**`HandleCommand` (IDM_DEVICE_BASE case)**: see `contracts/menu-command-ids.md` for the
full implementation spec.

**`HandleRaopConnected`**: kill `TIMER_HANDSHAKE_TIMEOUT`, commit `connectedReceiverIdx_ =
connectingReceiverIdx_`, set `connectingReceiverIdx_ = -1`, then do existing WASAPI/ALAC
startup. Write `config_.lastDevice = connectedReceiver_.stableId`.

**`HandleRaopFailed`**: distinguish pessimistic (connectingReceiverIdx_ >= 0) from mid-stream
failure; see `contracts/wm-messages.md`.

**`ShowTrayMenu`**: update the `trayMenu_.Show(...)` call to pass the new parameters:
```cpp
UINT cmd = trayMenu_.Show(hwnd_, config_, sparkle_.IsAvailable(),
                          bonjourMissing_,
                          sortedReceivers_,
                          connectedReceiverIdx_,
                          connectingReceiverIdx_);
```

**`Connect()`**: remove the `isConnected_ = true` line — it now moves to `HandleRaopConnected`.

---

## Running the Tests

```powershell
# From repo root:
cmake --preset msvc-x64-debug-ci
cmake --build --preset msvc-x64-debug-ci

# Run all unit tests (includes existing + new):
ctest --preset msvc-x64-debug-ci -L unit --output-on-failure

# Run only the discovery-related tests:
ctest --preset msvc-x64-debug-ci -L unit -R "txt|tray_menu|receiver_list"
```

---

## Manual Testing Checklist

1. **Discovery smoke test**: Launch AirBeam with a shairport-sync instance advertising on
   LAN. Right-click tray within 10 s → speaker name appears.
2. **AirPlay 2 filter**: If a device advertising with `pk=…` is present, confirm it does NOT
   appear in the menu.
3. **Searching placeholder**: Kill Bonjour service and relaunch → "Searching for speakers…"
   appears.
4. **Bonjour missing**: Rename `dnssd.dll` temporarily → "Install Bonjour to discover
   speakers" appears.
5. **4-speaker submenu**: Run four shairport-sync instances → "Speakers" submenu appears.
6. **Pessimistic checkmark**: Click a speaker that takes > 5 s to respond → checkmark does
   NOT move; "Connecting…" label disappears silently after 5 s.
7. **Cancel-and-redirect**: While "Connecting…" is shown for Speaker A, click Speaker B →
   A's label disappears, B's appears.
8. **Stale removal**: Kill one shairport-sync instance → device disappears from menu within
   65 s.
9. **Auto-select after restart**: Connect to "Living Room", close AirBeam, relaunch while
   "Living Room" is on the network → auto-connects within 10 s.
10. **Auto-select expiry**: Connect to "Living Room", close AirBeam, power off receiver,
    relaunch → after 30 s with no auto-select, device must NOT auto-connect even when
    powered back on.
