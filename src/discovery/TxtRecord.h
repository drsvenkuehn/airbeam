#pragma once
#include <cstdint>
#include "discovery/AirPlayReceiver.h"

namespace TxtRecord {

/// Parses a raw DNS-SD TXT record buffer and fills the relevant fields of `out`.
/// Sets isAirPlay1Compatible = true iff:
///   - "et" token "1" is present (RSA-AES), or "et" is a non-zero hex bitmask
/// Sets isAirPlay2Only = true iff:
///   - "pk" key is present (HomeKit public key) AND etHas1 = false
///   - Covers JBL BAR 300 (et=0 + pk) and Apple HomePod (et=0,3,5 + pk)
/// Sets supportsAes = true iff et=1 token present, hex non-zero, or no et field at all.
void Parse(const unsigned char* txt, uint16_t len, AirPlayReceiver& out);

} // namespace TxtRecord
