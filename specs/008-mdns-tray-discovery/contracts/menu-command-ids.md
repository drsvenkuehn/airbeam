# Contract: Tray Menu Command IDs and Menu Structure

**Branch**: `008-mdns-tray-discovery`  
**Files**: `src/core/Commands.h`, `src/ui/TrayMenu.h`, `src/ui/MenuIds.h`

---

## `TrayMenu::Show` — Updated Signature

```cpp
/// Creates the popup menu, calls TrackPopupMenu, destroys the menu, and
/// returns the selected IDM_* command (or 0 if dismissed without selecting).
///
/// Parameters:
///   hwnd                 — parent window for TrackPopupMenu
///   config               — checkmark/enable state for static items
///   sparkleAvailable     — enables "Check for Updates"
///   bonjourMissing       — when true, shows IDS_MENU_BONJOUR_MISSING placeholder
///                          and suppresses all speaker items
///   receivers            — sorted, AirPlay1-filtered speaker list
///   connectedReceiverIdx — index in receivers with checkmark (-1 = none)
///   connectingReceiverIdx — index showing "— Connecting…" suffix (-1 = none)
UINT Show(HWND hwnd, const Config& config, bool sparkleAvailable,
          bool bonjourMissing,
          const std::vector<AirPlayReceiver>& receivers,
          int connectedReceiverIdx,
          int connectingReceiverIdx);
```

The zero-argument `Show(hwnd, config, sparkleAvailable)` overload is **removed**; callers
pass all state explicitly. `AppController::ShowTrayMenu()` is the only call site.

---

## Menu Structure Specification

### When `bonjourMissing == true`

```
[grayed, disabled]  "Install Bonjour to discover speakers"   (IDS_MENU_BONJOUR_MISSING)
────────────────────────────────────────────────────────────────────────────────── (separator)
Volume
Low-Latency Mode   [✓ if enabled]
Launch at Startup  [✓ if enabled]
Open Log Folder
Check for Updates…
────────────────────────────────────────────────────────────────────────────────── (separator)
Quit AirBeam
```

### When `receivers.empty()` (and Bonjour is installed)

```
[grayed, disabled]  "Searching for speakers…"                (IDS_MENU_SEARCHING)
────────────────────────────────────────────────────────────────────────────────── (separator)
[static items…]
```

### When `receivers.size() <= 3` (inline items, no submenu)

```
[✓] Living Room (AppleTV5,3)                ← connectedReceiverIdx = 0
    Kitchen — Connecting…                   ← connectingReceiverIdx = 1, suffix from IDS_MENU_CONNECTING
    Bedroom                                 ← no checkmark, no label
────────────────────────────────────────────────────────────────────────────────── (separator)
[static items…]
```

### When `receivers.size() >= 4` (submenu)

```
Speakers ►                                  ← IDS_MENU_SPEAKERS, MF_POPUP
  └─ [✓] Living Room (AppleTV5,3)
  └─     Kitchen — Connecting…
  └─     Bedroom
  └─     Office
────────────────────────────────────────────────────────────────────────────────── (separator)
[static items…]
```

---

## Speaker Item Construction Rules

For each `AirPlayReceiver` at index `i` in `receivers`:

1. Start with `label = receiver.displayName`.
2. If `i == connectingReceiverIdx`: append `StringLoader::Load(IDS_MENU_CONNECTING)`.
   English value: `" \x2014 Connecting\x2026"` (em-dash, space, "Connecting…").
3. Set `flags = MF_STRING`.
4. If `i == connectedReceiverIdx`: add `MF_CHECKED`.
   *(Visual style note: `MF_CHECKED` (checkmark) is used throughout — not a radio-button style. This aligns with the Win32 submenu model, is simpler to implement, and supersedes the original "radio-button visual style" wording in FR-009, which has been updated to "checkmark visual style (MF_CHECKED)".)*
5. Command ID: `IDM_DEVICE_BASE + static_cast<UINT>(i)`.
6. Append to either the root menu (≤ 3 speakers) or the submenu (≥ 4 speakers).

**No `MF_GRAYED` or `MF_DISABLED` items**: AirPlay2-only devices are filtered at
`ReceiverList::Update` level and never reach `TrayMenu::Show`.

---

## `HandleCommand` — Speaker Item Dispatch

```cpp
// In AppController::HandleCommand, default case:
if (id >= IDM_DEVICE_BASE && id < IDM_DEVICE_BASE + IDM_DEVICE_MAX_COUNT) {
    int idx = static_cast<int>(id - IDM_DEVICE_BASE);
    if (idx >= static_cast<int>(sortedReceivers_.size())) return;

    // Idempotent: clicking the connected speaker does nothing (FR §2.2)
    if (isConnected_ && idx == connectedReceiverIdx_) return;

    // Cancel-and-redirect: cancel in-flight handshake if one exists (FR-016c)
    if (connectingReceiverIdx_ >= 0) {
        KillTimer(hwnd_, TIMER_HANDSHAKE_TIMEOUT);
        if (raopSession_) { raopSession_->Stop(); raopSession_.reset(); }
        // priorConnectedIdx_ already set from the previous click — leave unchanged
        connectingReceiverIdx_ = -1;
    } else {
        // First click during idle: save current connected idx for potential revert
        priorConnectedIdx_ = connectedReceiverIdx_;
    }

    // Start pessimistic connection attempt
    connectingReceiverIdx_ = idx;
    Connect(sortedReceivers_[idx]);
    SetTimer(hwnd_, TIMER_HANDSHAKE_TIMEOUT, 5000, nullptr);
}
```

**Invariant maintained**: `connectingReceiverIdx_ >= 0` iff `TIMER_HANDSHAKE_TIMEOUT` is
active. These are always set and killed together.

---

## New Resource IDs

Added to `resources/resource_ids.h` (range 1028–1031):

```cpp
// ── Speaker menu dynamic strings ─────────────────────────────────────────────
#define IDS_MENU_SEARCHING       1028   // "Searching for speakers…"
#define IDS_MENU_BONJOUR_MISSING 1029   // "Install Bonjour to discover speakers"
#define IDS_MENU_CONNECTING      1030   // " — Connecting…" (suffix)
#define IDS_MENU_SPEAKERS        1031   // "Speakers" (submenu header)
```

---

## Locale Strings — English Canonical

```rc
IDS_MENU_SEARCHING       "Searching for speakers\x2026"
IDS_MENU_BONJOUR_MISSING "Install Bonjour to discover speakers"
IDS_MENU_CONNECTING      " \x2014 Connecting\x2026"
IDS_MENU_SPEAKERS        "Speakers"
```

All 7 locale files (`strings_en.rc`, `strings_de.rc`, `strings_fr.rc`, `strings_es.rc`,
`strings_ja.rc`, `strings_zh-Hans.rc`, `strings_ko.rc`) must contain these four entries.

---

## Unit Test Coverage

Tests in `tests/unit/test_tray_menu.cpp`:

| Test case | Description |
|-----------|-------------|
| `TrayMenu.BonjourMissing_ShowsInstallItem` | `bonjourMissing=true` → only install-Bonjour item in speaker section |
| `TrayMenu.EmptyReceivers_ShowsSearching` | `receivers.empty()` → "Searching for speakers…" grayed item |
| `TrayMenu.ThreeSpeakers_Inline` | 3 receivers → no submenu; all three as root items |
| `TrayMenu.FourSpeakers_Submenu` | 4 receivers → "Speakers" submenu contains all four |
| `TrayMenu.ConnectedIdx_ShowsCheckmark` | `connectedReceiverIdx=1` → item 1 has `MF_CHECKED` |
| `TrayMenu.ConnectingIdx_ShowsLabel` | `connectingReceiverIdx=2` → item 2 label ends with "— Connecting…" |
| `TrayMenu.AlphaOrder_Preserved` | Receivers passed in sorted order; menu preserves that order |

*Note*: `TrayMenu` tests use a headless Win32 approach — `CreatePopupMenu` can be called
without a visible window; item text and flags are verified via `GetMenuItemInfo`. The
`TrackPopupMenu` path is not unit-tested (integration concern).
