#include <gtest/gtest.h>
#include "protocol/VolumeMapper.h"

// ── LinearToDb ────────────────────────────────────────────────────────────────

TEST(VolumeMapperTest, Zero_MapsToMinDb)
{
    // linear == 0.0 must map to exactly -144.0 (RAOP mute floor).
    EXPECT_NEAR(VolumeMapper::LinearToDb(0.0f), -144.0f, 0.01f);
}

TEST(VolumeMapperTest, One_MapsToZeroDb)
{
    // linear == 1.0 must map to 0.0 dB (full volume).
    EXPECT_NEAR(VolumeMapper::LinearToDb(1.0f), 0.0f, 0.01f);
}

TEST(VolumeMapperTest, Half_MapsToMinusSixDb)
{
    // 20 * log10(0.5) == -6.0206...  → approximately -6.02 dB.
    EXPECT_NEAR(VolumeMapper::LinearToDb(0.5f), -6.02f, 0.01f);
}

// ── DbToLinear round-trip ─────────────────────────────────────────────────────

TEST(VolumeMapperTest, RoundTrip_075_WithinTolerance)
{
    // DbToLinear(LinearToDb(0.75)) should return a value within ±0.001 of 0.75.
    const float db     = VolumeMapper::LinearToDb(0.75f);
    const float linear = VolumeMapper::DbToLinear(db);
    EXPECT_NEAR(linear, 0.75f, 0.001f);
}

// ── DbToLinear boundary ───────────────────────────────────────────────────────

TEST(VolumeMapperTest, MinDb_MapsToZero)
{
    EXPECT_FLOAT_EQ(VolumeMapper::DbToLinear(-144.0f), 0.0f);
}

TEST(VolumeMapperTest, BelowMinDb_MapsToZero)
{
    EXPECT_FLOAT_EQ(VolumeMapper::DbToLinear(-200.0f), 0.0f);
}

TEST(VolumeMapperTest, ZeroDb_MapsToOne)
{
    EXPECT_NEAR(VolumeMapper::DbToLinear(0.0f), 1.0f, 0.0001f);
}

// ── Negative linear values clamp to mute floor ───────────────────────────────

TEST(VolumeMapperTest, NegativeLinear_ClampedToMinDb)
{
    EXPECT_NEAR(VolumeMapper::LinearToDb(-1.0f), -144.0f, 0.01f);
}
