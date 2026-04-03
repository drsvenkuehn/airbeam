#pragma once

#include <windows.h>
#include <string>
#include <cstdint>

/// Describes one AirPlay receiver discovered via mDNS (_raop._tcp).
struct AirPlayReceiver {
    std::wstring instanceName;          ///< mDNS service instance name
    std::wstring displayName;           ///< human-readable label shown in menu
    std::wstring hostName;              ///< e.g. "AppleTV.local"
    std::string  ipAddress;             ///< dotted-decimal or IPv6 literal
    uint16_t     port                 = 5000;
    bool         isAirPlay1Compatible = false; ///< true → speaks RAOP (AirPlay 1)
    bool         isAirPlay2Only       = false; ///< true → pk present + no RSA-AES → AirPlay 2 only device
    bool         supportsAes          = false; ///< true → RSA-AES encryption accepted
    std::string  macAddress;            ///< from TXT record "et" or "am" field
    std::string  deviceModel;           ///< e.g. "AppleTV3,2"
    std::string  protocolVersion;       ///< e.g. "130.14"
    std::wstring stableId;                  ///< stable MAC-based device ID (MAC prefix from instance name)
    ULONGLONG    lastSeenTick         = 0;  ///< GetTickCount64() for stale-entry detection
};
