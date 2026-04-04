#pragma once

#include <windows.h>
#include <string>
#include <cstdint>

/// Pairing state for an AirPlay 2 receiver.
enum class PairingState : uint8_t {
    NotApplicable = 0,  ///< AirPlay 1 only — no HAP pairing
    Unpaired      = 1,  ///< AP2 capable, not yet paired with this controller
    Pairing       = 2,  ///< HAP pairing ceremony in progress
    Paired        = 3,  ///< Successfully paired — can stream
    Error         = 4,  ///< Pairing failed or credential mismatch
};

/// Describes one AirPlay receiver discovered via mDNS (_raop._tcp or _airplay._tcp).
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
    std::wstring stableId;              ///< stable MAC-based device ID (MAC prefix from instance name)
    ULONGLONG    lastSeenTick         = 0;  ///< GetTickCount64() for stale-entry detection

    // ── AirPlay 2 fields (Feature 010) ────────────────────────────────────────
    bool         supportsAirPlay2     = false; ///< true iff pk TXT field is present
    std::string  hapDevicePublicKey;    ///< base64-encoded Ed25519 device LTPK from TXT "pk" field
    PairingState pairingState         = PairingState::NotApplicable; ///< HAP pairing status
    uint16_t     airPlay2Port         = 7000;  ///< AirPlay 2 control port (default 7000)
};

