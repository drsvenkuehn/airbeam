#pragma once
#include <cstdint>
#include "discovery/AirPlayReceiver.h"

namespace TxtRecord {

/// Parses a raw DNS-SD TXT record buffer and fills the relevant fields of `out`.
/// Sets isAirPlay1Compatible = true iff:
///   - "et" value contains token "1" (AirPlay 1 audio encryption support)
///   - "pk" key is ABSENT (pk present means AirPlay 2 HomeKit pairing required)
void Parse(const unsigned char* txt, uint16_t len, AirPlayReceiver& out);

} // namespace TxtRecord
