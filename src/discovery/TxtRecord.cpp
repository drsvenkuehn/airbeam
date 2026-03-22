#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "discovery/TxtRecord.h"
#include <cstring>
#include <sstream>
#include <string>

namespace TxtRecord {

void Parse(const unsigned char* txt, uint16_t len, AirPlayReceiver& out)
{
    if (!txt || len == 0) return;

    bool etHas1    = false;
    bool pkPresent = false;

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
            // Tokenize by comma; AirPlay 1 audio support is token "1"
            std::istringstream ss(value);
            std::string token;
            while (std::getline(ss, token, ','))
            {
                if (token == "1")
                {
                    etHas1 = true;
                    break;
                }
            }
        }
        else if (key == "pk")
        {
            pkPresent = true;
        }
        else if (key == "am")
        {
            out.deviceModel = value;
        }
        else if (key == "vs")
        {
            out.protocolVersion = value;
        }
        // "md" and "tp" parsed but have no corresponding AirPlayReceiver fields
    }

    out.isAirPlay1Compatible = etHas1 && !pkPresent;
}

} // namespace TxtRecord
