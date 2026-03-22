/// T083: 24-hour stress test: stream continuously and verify:
///   - No heap growth (monitored externally via WinDbg / Application Verifier)
///   - RTP timestamp monotonically increasing (no drift)
///   - droppedFrameCount and udpDropCount within acceptable thresholds
///   - No crashes or assertions
///
/// Run with: ctest -R stress_24h --output-on-failure
/// Requires: shairport-sync Docker container running (docker-compose up -d)
///
/// DISABLED by default — must be explicitly enabled via AIRBEAM_STRESS_TEST=1 env var.
#include <gtest/gtest.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>

// TODO(T049-T053): include ConnectionController when available

TEST(StressTest, DISABLED_24HourContinuousStream) {
#pragma warning(suppress: 4996)
    const char* enabled = getenv("AIRBEAM_STRESS_TEST");
    if (!enabled || std::string(enabled) != "1")
        GTEST_SKIP() << "Set AIRBEAM_STRESS_TEST=1 to run the 24h stress test";

    // 1. Connect to shairport-sync container
    // TODO: initiate RAOP session via ConnectionController

    // 2. Stream for 24 hours (86400 seconds)
    // Monitor via WinDbg / Application Verifier externally

    // 3. Sample counters every 60s
    constexpr DWORD    kDurationSec    = 86400;
    constexpr DWORD    kSampleInterval = 60 * 1000; // ms
    constexpr uint64_t kMaxDropRate    = 10;         // drops per minute acceptable

    DWORD    start         = GetTickCount();
    uint64_t lastDropCount = 0;
    uint64_t lastRtpTs     = 0;
    bool     rtpMonotonicity = true;

    while (GetTickCount() - start < kDurationSec * 1000) {
        Sleep(kSampleInterval);

        // TODO: query droppedFrameCount from WasapiCapture
        // TODO: query current RTP timestamp from AlacEncoderThread
        // For now: placeholder assertions
        uint64_t dropCount = 0; // placeholder
        uint64_t rtpTs     = 0; // placeholder

        uint64_t dropDelta = dropCount - lastDropCount;
        EXPECT_LE(dropDelta, kMaxDropRate) << "Drop rate exceeded threshold";

        if (lastRtpTs != 0) {
            EXPECT_GT(rtpTs, lastRtpTs) << "RTP timestamp not monotonically increasing";
            rtpMonotonicity = rtpMonotonicity && (rtpTs > lastRtpTs);
        }

        lastDropCount = dropCount;
        lastRtpTs     = rtpTs;
    }

    EXPECT_TRUE(rtpMonotonicity) << "RTP timestamp drift detected during 24h stream";
}
