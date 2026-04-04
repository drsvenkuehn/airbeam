# Contract: TXT Record Parsing

**Branch**: `008-mdns-tray-discovery`  
**File**: `src/discovery/TxtRecord.cpp` + `src/discovery/AirPlayReceiver.h`

---

## `TxtRecord::Parse` — Updated Contract

```cpp
namespace TxtRecord {

/// Parses a raw DNS-SD TXT record buffer and fills the relevant fields of `out`.
///
/// Pre-conditions:
///   - `out.displayName` is already set from the mDNS instance name as a fallback
///     (set by MdnsDiscovery::BrowseCallback before starting the resolve pipeline).
///   - `out.stableId` is already set from DeviceIdFromInstance in BrowseCallback.
///
/// Post-conditions on `out` after Parse():
///   - `isAirPlay1Compatible` = true iff et contains token "1" AND pk is absent.
///   - `deviceModel`          = am field value (UTF-8 string), or empty string.
///   - `protocolVersion`      = vs field value, or empty string.
///   - `displayName`          = if `an` present: wide(an) [+ " (" + wide(am) + ")"]
///                              else: unchanged (instance-name fallback preserved).
///                              Always truncated to ≤ 40 wide characters (U+2026 appended).
///
/// Fields NOT touched by Parse():
///   - instanceName, stableId, hostName, ipAddress, port, lastSeenTick
void Parse(const unsigned char* txt, uint16_t len, AirPlayReceiver& out);

} // namespace TxtRecord
```

---

## AirPlay 1 Filter Specification

A device is AirPlay 1-compatible (and included in `ReceiverList`) if and only if:

```
TXT["et"] contains comma-separated token "1"
  AND
TXT["pk"] key is ABSENT
```

These conditions map to the spec (FR-003):
- `et` = encryption types; value `"1"` = AES-128-CBC (required for RAOP).
- `pk` = public key; presence indicates AirPlay 2 HomeKit pairing is required.

**Examples**:

| `et` value | `pk` present? | Compatible? | Notes |
|------------|---------------|-------------|-------|
| `"1"` | No | ✅ Yes | Minimal AirPlay 1 device |
| `"0,1"` | No | ✅ Yes | Supports both unencrypted (0) and AES (1) |
| `"0,1,4"` | No | ✅ Yes | Extra encryption types don't disqualify |
| `"4"` | No | ❌ No | No AES-128; RAOP not supported |
| `"1"` | Yes | ❌ No | AirPlay 2 HomeKit device |
| `"0,1"` | Yes | ❌ No | AirPlay 2 device that can also do AirPlay 1, but HomeKit pairing required |
| *(missing)* | No | ❌ No | Non-compliant device |

---

## Display Name Truncation Specification

```
MAX_DISPLAY_CHARS = 40
ELLIPSIS = U+2026 (…)

composed:
  if an_field is non-empty:
    composed = to_wide(an_field)
    if am_field is non-empty:
      composed += L" (" + to_wide(am_field) + L")"
  else:
    composed = out.displayName  (pre-populated fallback)

if composed.length() > MAX_DISPLAY_CHARS:
  out.displayName = composed.substr(0, MAX_DISPLAY_CHARS - 1) + L"\u2026"
else:
  out.displayName = composed
```

**Examples**:

| `an` | `am` | Result (≤ 40 chars) |
|------|------|---------------------|
| `"Living Room"` | `"AppleTV5,3"` | `"Living Room (AppleTV5,3)"` (26 chars) |
| `"Kitchen"` | `""` | `"Kitchen"` (7 chars) |
| `""` | `"AirPortExpress2,1"` | *(fallback from instance name)* |
| `"A very long speaker name that exceeds"` | `"Model9,1"` | `"A very long speaker name that exc…"` (40 chars) |
| `""` | `""` | *(fallback from instance name)* |

---

## `DeviceIdFromInstance` Specification

New helper function (defined in `MdnsDiscovery.cpp`, anonymous namespace):

```cpp
/// Extract the stable device identifier from a _raop._tcp service instance name.
/// RAOP names have the form "AA:BB:CC:DD:EE:FF@DeviceName".
/// Returns the substring before '@' (the MAC address), uppercased.
/// If no '@' is present, returns the full instance name.
std::wstring DeviceIdFromInstance(const std::wstring& instanceName)
{
    const auto at = instanceName.find(L'@');
    return (at != std::wstring::npos) ? instanceName.substr(0, at) : instanceName;
}
```

**Post-condition**: Result is always uppercase (A–F, 0–9, colons only).

**Usage**: Called in `MdnsDiscovery::BrowseCallback` to populate
`pendingResolve_.receiver.stableId`.

---

## Unit Test Coverage for This Contract

Tests live in `tests/unit/test_mdns_txt.cpp`.

| Test case | Description |
|-----------|-------------|
| `TxtRecord.AnField_SetsDisplayName` | `an=Living Room` → `displayName = L"Living Room"` |
| `TxtRecord.AnAndAm_CombinedDisplayName` | `an=Kitchen, am=AppleTV5,3` → `L"Kitchen (AppleTV5,3)"` |
| `TxtRecord.LongName_Truncated` | 50-char composed name → 40 chars with `…` |
| `TxtRecord.NoAn_FallbackPreserved` | No `an` key → `displayName` unchanged from pre-populated value |
| `TxtRecord.Et1NoKey_IsAirPlay1` | *(existing)* |
| `TxtRecord.Et01NoKey_IsAirPlay1` | *(existing)* |
| `TxtRecord.Et4Only_NotAirPlay1` | *(existing)* |
| `TxtRecord.PkPresent_NotAirPlay1` | *(existing)* |
| `TxtRecord.EmptyTxt_NotAirPlay1` | *(existing)* |
| `TxtRecord.AmAndVsParsed` | *(existing)* |
