#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "discovery/TxtRecord.h"
#include <cstring>
#include <sstream>
#include <string>

namespace TxtRecord {

void Parse(const unsigned char* txt, uint16_t len, AirPlayReceiver& out)
{
    if (!txt || len == 0) return;

    bool etHas1      = false; // RSA-AES capable: et token "1" OR non-zero hex bitmask
    bool etHasAes    = false; // same as etHas1 (kept for future granularity)
    bool etPresent   = false; // any et field was seen
    bool pkPresent   = false;
    std::string anField;

    uint16_t offset = 0;
    while (offset < len)
    {
        const uint8_t entryLen = txt[offset++];
        if (offset + entryLen > len) break; // malformed record

        const char* entry = reinterpret_cast<const char*>(txt + offset);
        offset += entryLen;

        if (entryLen == 0) continue;

        // Split on first '='
        const char* eq = static_cast<const char*>(std::memchr(entry, '=', entryLen));

        std::string key;
        std::string value;
        if (eq)
        {
            const std::size_t keyLen = static_cast<std::size_t>(eq - entry);
            key   = std::string(entry, keyLen);
            value = std::string(eq + 1, entryLen - keyLen - 1);
        }
        else
        {
            key = std::string(entry, entryLen);
        }

        if (key == "et")
        {
            etPresent = true;
            // Two formats exist in the wild:
            //
            // A) Comma-separated decimal tokens (modern devices):
            //    et=0,1,3  →  "0"=unencrypted, "1"=RSA/AES, "3"=FairPlay, "5"=MFiSAP
            //    Only token "1" signals RSA-AES (AirPlay 1) capability.
            //    Tokens "0", "3", "5" alone are NOT sufficient — Apple HomePod advertises
            //    et=0,3,5 (FairPlay/MFiSAP) but does NOT support RSA-AES RAOP (→ 406).
            //
            // B) Hex bitmask (shairport-sync, AirPort Express, classic Apple devices):
            //    et=0x4  →  any non-zero hex value means RAOP / RSA-AES capable.
            //
            // AirPlay 2-only detection (see isAirPlay2Only below):
            //    pk present AND etHas1=false → device has HomeKit key but no RSA-AES
            //    support → AirPlay 2 only. Covers both JBL (et=0) and HomePod (et=0,3,5).

            const bool isHex = (value.size() > 2 &&
                                 value[0] == '0' &&
                                 (value[1] == 'x' || value[1] == 'X'));
            if (isHex)
            {
                // Any non-zero hex et means the device supports RAOP RSA-AES
                const unsigned long bits = std::strtoul(value.c_str(), nullptr, 16);
                if (bits != 0) {
                    etHas1   = true;
                    etHasAes = true;
                }
            }
            else
            {
                std::istringstream ss(value);
                std::string token;
                while (std::getline(ss, token, ','))
                {
                    if (token == "1")
                    {
                        etHas1   = true; // RSA-AES explicitly advertised
                        etHasAes = true;
                    }
                }
            }
        }
        else if (key == "pk")
        {
            pkPresent = true;
            out.hapDevicePublicKey = value;  // base64-encoded Ed25519 device LTPK
        }
        else if (key == "vv")
        {
            // vv=2 indicates AirPlay 2 protocol version
            // (informational — supportsAirPlay2 is set by pk presence)
        }
        else if (key == "am")
        {
            out.deviceModel = value;
        }
        else if (key == "vs")
        {
            out.protocolVersion = value;
        }
        else if (key == "an")
        {
            anField = value;
        }
        // "md" and "tp" parsed but have no corresponding AirPlayReceiver fields
    }

    // AirPlay 2-only: device has a HomeKit public key (pk) but does NOT advertise
    // RSA-AES support (et=1 or non-zero hex). This covers:
    //   JBL BAR 300:  et=0,     pk → etHas1=false → AirPlay 2 only ✓
    //   Apple HomePod: et=0,3,5, pk → etHas1=false → AirPlay 2 only ✓
    // Apple AirPort Express (et=0x4, no pk) and shairport-sync (et=0x4, no pk):
    //   etHas1=true → isAirPlay2Only=false → AirPlay 1 ✓
    out.isAirPlay2Only       = pkPresent && !etHas1;
    out.isAirPlay1Compatible = etHas1 && !out.isAirPlay2Only; // etHas1 implies !isAirPlay2Only

    // AirPlay 2 support: any receiver with pk field supports AP2
    out.supportsAirPlay2 = pkPresent;

    // Set pairingState for AP2 receivers (actual pairing status loaded from CredentialStore)
    if (out.supportsAirPlay2 && out.pairingState == PairingState::NotApplicable)
        out.pairingState = PairingState::Unpaired;

    // airPlay2Port: use port from _airplay._tcp browse (set by MdnsDiscovery for AP2 receivers).
    // For _raop._tcp discoveries the RAOP port is in 'port' — AP2 defaults to 7000.
    if (out.airPlay2Port == 7000 && out.supportsAirPlay2) {
        // Keep default 7000; MdnsDiscovery can override after _airplay._tcp resolve
    }

    // Use RSA-AES only when the device explicitly supports it (et=1 or non-zero hex).
    // If no et field at all, default to encryption (safer).
    // Devices that lack et=1 but connect anyway (AirPlay 2 only) won't reach this path.
    out.supportsAes = !etPresent || etHas1;

    // Build display name from 'an' and 'am' fields
    if (!anField.empty())
    {
        auto Utf8ToWide = [](const std::string& s) -> std::wstring {
            if (s.empty()) return {};
            const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
            if (len <= 0) return {};
            std::wstring w(static_cast<std::size_t>(len - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
            return w;
        };

        std::wstring composed = Utf8ToWide(anField);
        if (!out.deviceModel.empty())
            composed += L" (" + Utf8ToWide(out.deviceModel) + L")";

        constexpr std::size_t kMaxDisplayChars = 40;
        if (composed.length() > kMaxDisplayChars)
            out.displayName = composed.substr(0, kMaxDisplayChars - 1) + L"\u2026";
        else
            out.displayName = composed;
    }
}

} // namespace TxtRecord
