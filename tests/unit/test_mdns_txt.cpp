// T023 — mDNS TXT record parsing unit tests
#include <gtest/gtest.h>
#include "discovery/TxtRecord.h"
#include "discovery/AirPlayReceiver.h"

#include <string>
#include <vector>
#include <initializer_list>

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

TEST(TxtRecord, Et4Only_NotAirPlay1)
{
    auto buf = BuildTxt({"et=4"});
    AirPlayReceiver r{};
    TxtRecord::Parse(buf.data(), static_cast<uint16_t>(buf.size()), r);
    EXPECT_FALSE(r.isAirPlay1Compatible);
}

TEST(TxtRecord, PkPresent_NotAirPlay1)
{
    // A "pk" key signals AirPlay 2 — override AirPlay 1 flag.
    auto buf = BuildTxt({"et=1", "pk=abc123"});
    AirPlayReceiver r{};
    TxtRecord::Parse(buf.data(), static_cast<uint16_t>(buf.size()), r);
    EXPECT_FALSE(r.isAirPlay1Compatible);
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
