#pragma once
#include <cstdint>

namespace NtpClock {
    /// NTP epoch offset: number of seconds between 1900-01-01 and 1970-01-01.
    constexpr uint64_t kNtpEpochOffset = 2208988800ULL;

    /// Returns the current time as NTP seconds (seconds since 1900-01-01).
    uint32_t NowSeconds();

    /// Returns the full NTP 64-bit timestamp:
    ///   upper 32 bits = seconds since 1900-01-01
    ///   lower 32 bits = fraction (2^32 counts per second)
    uint64_t NowNtp64();
}
