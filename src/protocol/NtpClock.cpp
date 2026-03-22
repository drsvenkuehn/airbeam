#include "protocol/NtpClock.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Windows FILETIME epoch is 1601-01-01; Unix epoch is 1970-01-01.
// Difference in 100-nanosecond intervals:
static constexpr ULONGLONG kFileTimeToUnix = 116444736000000000ULL;

uint32_t NtpClock::NowSeconds()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    ULONGLONG ticks = (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32)
                    |  static_cast<ULONGLONG>(ft.dwLowDateTime);

    uint64_t unixSec = (ticks - kFileTimeToUnix) / 10000000ULL;
    return static_cast<uint32_t>(unixSec + kNtpEpochOffset);
}

uint64_t NtpClock::NowNtp64()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    ULONGLONG ticks = (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32)
                    |  static_cast<ULONGLONG>(ft.dwLowDateTime);

    ULONGLONG ticksSinceUnix = ticks - kFileTimeToUnix;
    uint64_t  unixSec        = ticksSinceUnix / 10000000ULL;
    uint64_t  ntpSec         = unixSec + kNtpEpochOffset;

    // Sub-second part in 100ns ticks
    uint64_t frac100ns = ticksSinceUnix % 10000000ULL;
    // Scale to 2^32 fractions per second
    uint32_t frac32    = static_cast<uint32_t>((frac100ns * (1ULL << 32)) / 10000000ULL);

    return (ntpSec << 32) | frac32;
}
