// T023 — mDNS TXT record parsing unit tests
#include <gtest/gtest.h>
#include "discovery/TxtRecord.h"
#include "discovery/AirPlayReceiver.h"

#include <string>
#include <vector>
#include <initializer_list>
#include <algorithm>

// ── Helper ────────────────────────────────────────────────────────────────────

static std::vector<uint8_t> BuildTxt(std::initializer_list<std::string> kvs)
{
    std::vector<uint8_t> buf;
    for (const auto& kv : kvs) {
        buf.push_back(static_cast<uint8_t>(kv.size()));
        buf.insert(buf.end(), kv.begin(), kv.end());
    }
    return buf;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(TxtRecord, Et1NoKey_IsAirPlay1)
{
    auto buf = BuildTxt({"et=1"});
    AirPlayReceiver r{};
    TxtRecord::Parse(buf.data(), static_cast<uint16_t>(buf.size()), r);
    EXPECT_TRUE(r.isAirPlay1Compatible);
}

TEST(TxtRecord, Et01NoKey_IsAirPlay1)
{
    // "et=0,1" contains value "1" in the comma-separated list
    auto buf = BuildTxt({"et=0,1"});
    AirPlayReceiver r{};
    TxtRecord::Parse(buf.data(), static_cast<uint16_t>(buf.size()), r);
    EXPECT_TRUE(r.isAirPlay1Compatible);
}

TEST(TxtRecord, EtHexNonZero_IsAirPlay1)
{
    // shairport-sync / AirPort Express: et=0x4 hex bitmask → AirPlay 1 capable.
    auto buf = BuildTxt({"et=0x4"});
    AirPlayReceiver r{};
    TxtRecord::Parse(buf.data(), static_cast<uint16_t>(buf.size()), r);
    EXPECT_TRUE(r.isAirPlay1Compatible);
    EXPECT_FALSE(r.isAirPlay2Only);
    EXPECT_TRUE(r.supportsAes);
}

TEST(TxtRecord, Et4Only_NotAirPlay1)
{
    auto buf = BuildTxt({"et=4"});
    AirPlayReceiver r{};
    TxtRecord::Parse(buf.data(), static_cast<uint16_t>(buf.size()), r);
    EXPECT_FALSE(r.isAirPlay1Compatible);
}

TEST(TxtRecord, Et0PkPresent_IsAirPlay2Only)
{
    // JBL BAR 300: et=0 + pk → no RSA-AES capability + HomeKit key → AirPlay 2 only.
    auto buf = BuildTxt({"et=0", "pk=abc123"});
    AirPlayReceiver r{};
    TxtRecord::Parse(buf.data(), static_cast<uint16_t>(buf.size()), r);
    EXPECT_FALSE(r.isAirPlay1Compatible);
    EXPECT_TRUE(r.isAirPlay2Only);
}

TEST(TxtRecord, Et035PkPresent_IsAirPlay2Only)
{
    // Apple HomePod: et=0,3,5 + pk → FairPlay/MFiSAP only, no RSA-AES → AirPlay 2 only.
    auto buf = BuildTxt({"et=0,3,5", "pk=abc123"});
    AirPlayReceiver r{};
    TxtRecord::Parse(buf.data(), static_cast<uint16_t>(buf.size()), r);
    EXPECT_FALSE(r.isAirPlay1Compatible);
    EXPECT_TRUE(r.isAirPlay2Only);
}

TEST(TxtRecord, Et1PkPresent_IsAirPlay1)
{
    // A device advertising both et=1 (RSA-AES) and pk (HomeKit) supports AirPlay 1.
    auto buf = BuildTxt({"et=1", "pk=abc123"});
    AirPlayReceiver r{};
    TxtRecord::Parse(buf.data(), static_cast<uint16_t>(buf.size()), r);
    EXPECT_TRUE(r.isAirPlay1Compatible);
    EXPECT_FALSE(r.isAirPlay2Only);
}

TEST(TxtRecord, EmptyTxt_NotAirPlay1)
{
    AirPlayReceiver r{};
    TxtRecord::Parse(nullptr, 0, r);
    EXPECT_FALSE(r.isAirPlay1Compatible);
}

TEST(TxtRecord, AmAndVsParsed)
{
    auto buf = BuildTxt({"et=1", "am=AppleTV3,2", "vs=130.14"});
    AirPlayReceiver r{};
    TxtRecord::Parse(buf.data(), static_cast<uint16_t>(buf.size()), r);
    EXPECT_EQ(r.deviceModel,       "AppleTV3,2");
    EXPECT_EQ(r.protocolVersion,   "130.14");
}

TEST(TxtRecord, AnField_SetsDisplayName)
{
    auto buf = BuildTxt({"et=1", "an=Living Room"});
    AirPlayReceiver r{};
    TxtRecord::Parse(buf.data(), static_cast<uint16_t>(buf.size()), r);
    EXPECT_EQ(r.displayName, L"Living Room");
}

TEST(TxtRecord, AnAndAm_CombinedDisplayName)
{
    auto buf = BuildTxt({"et=1", "an=My Speaker", "am=AppleTV3,2"});
    AirPlayReceiver r{};
    TxtRecord::Parse(buf.data(), static_cast<uint16_t>(buf.size()), r);
    EXPECT_EQ(r.displayName, L"My Speaker (AppleTV3,2)");
}

TEST(TxtRecord, LongName_Truncated)
{
    // 50-char name should be truncated to 39 chars + ellipsis (40 total)
    std::string longName = "an=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"; // 51 chars = "an=" + 48 A's
    auto buf = BuildTxt({"et=1", longName});
    AirPlayReceiver r{};
    TxtRecord::Parse(buf.data(), static_cast<uint16_t>(buf.size()), r);
    EXPECT_EQ(r.displayName.size(), 40u);
    EXPECT_EQ(r.displayName.back(), L'\u2026');
}

TEST(TxtRecord, NoAn_FallbackPreserved)
{
    // No 'an' field: displayName should remain empty (not set by TxtRecord::Parse)
    auto buf = BuildTxt({"et=1", "am=AppleTV3,2"});
    AirPlayReceiver r{};
    r.displayName = L"Preset Name"; // set before parse
    TxtRecord::Parse(buf.data(), static_cast<uint16_t>(buf.size()), r);
    EXPECT_EQ(r.displayName, L"Preset Name"); // unchanged
}

// Mirror of MdnsDiscovery anonymous-namespace helper for testing
static std::wstring DeviceIdFromInstance_TestHelper(const std::wstring& instanceName)
{
    const auto at = instanceName.find(L'@');
    std::wstring id = (at != std::wstring::npos) ? instanceName.substr(0, at) : instanceName;
    std::transform(id.begin(), id.end(), id.begin(), ::towupper);
    return id;
}

TEST(TxtRecord, DeviceIdFromInstance_LowercaseInput_ReturnsUppercase)
{
    EXPECT_EQ(DeviceIdFromInstance_TestHelper(L"aa:bb:cc:dd:ee:ff@MyDevice"),
              L"AA:BB:CC:DD:EE:FF");
    EXPECT_EQ(DeviceIdFromInstance_TestHelper(L"NoAtSign"),
              L"NOATSIGN");
}
