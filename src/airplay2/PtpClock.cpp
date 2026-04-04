// src/airplay2/PtpClock.cpp
// Simplified PTP sync loop for AirPlay 2 multi-room timing.
// Runs entirely on Thread 5 (RTSP/AP2 session control).
// §I: Only ClockOffset() (atomic read) is safe from Thread 3/4 hot path.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstring>
#include <cstdint>
#include <algorithm>

#include "airplay2/PtpClock.h"
#include "core/Logger.h"

#pragma comment(lib, "ws2_32.lib")

namespace AirPlay2 {

// ────────────────────────────────────────────────────────────────────────────
// PTP packet types (simplified subset used by AirPlay 2)
// ────────────────────────────────────────────────────────────────────────────

static constexpr uint8_t kPtpSync        = 0x00;
static constexpr uint8_t kPtpFollowUp    = 0x08;
static constexpr uint8_t kPtpDelayReq    = 0x01;
static constexpr uint8_t kPtpDelayResp   = 0x09;

// Minimal PTP header (44 bytes)
#pragma pack(push, 1)
struct PtpHeader {
    uint8_t  msgType;       // lower nibble = message type
    uint8_t  versionPTP;    // 0x02
    uint16_t msgLen;        // big-endian
    uint8_t  domainNum;
    uint8_t  reserved1;
    uint16_t flags;
    int64_t  correctionField; // big-endian
    uint32_t reserved2;
    uint8_t  sourcePortId[10];
    uint16_t seqId;         // big-endian
    uint8_t  control;
    int8_t   logMsgInterval;
};

struct PtpTimestamp {
    uint16_t secondsHi;
    uint32_t secondsLo;
    uint32_t nanoseconds;
};
#pragma pack(pop)

// ────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ────────────────────────────────────────────────────────────────────────────

PtpClock::PtpClock()
{
    QueryPerformanceFrequency(&qpcFreq_);
}

PtpClock::~PtpClock()
{
    Stop();
}

// ────────────────────────────────────────────────────────────────────────────
// NowNs — high-resolution nanoseconds
// ────────────────────────────────────────────────────────────────────────────

/*static*/ int64_t PtpClock::NowNs()
{
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return (now.QuadPart * 1'000'000'000LL) / freq.QuadPart;
}

// ────────────────────────────────────────────────────────────────────────────
// Start
// ────────────────────────────────────────────────────────────────────────────

bool PtpClock::Start(const std::string& deviceIp, uint16_t ptpPort)
{
    Stop();
    stop_.store(false, std::memory_order_release);

    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCKET) {
        LOG_WARN("PtpClock: socket() failed (%d)", WSAGetLastError());
        return false;
    }

    // Set receive timeout 250 ms
    DWORD tv = 250;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));

    std::memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;
    addr_.sin_port   = htons(ptpPort);
    if (inet_pton(AF_INET, deviceIp.c_str(), &addr_.sin_addr) != 1) {
        LOG_WARN("PtpClock: invalid device IP \"%s\"", deviceIp.c_str());
        closesocket(sock_); sock_ = INVALID_SOCKET;
        return false;
    }

    LOG_INFO("PtpClock: started for %s:%u", deviceIp.c_str(), ptpPort);
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// Stop
// ────────────────────────────────────────────────────────────────────────────

void PtpClock::Stop()
{
    stop_.store(true, std::memory_order_release);
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// SendSync — send a DELAY_REQ and record t1
// ────────────────────────────────────────────────────────────────────────────

bool PtpClock::SendSync(int64_t& t1Ns)
{
    if (sock_ == INVALID_SOCKET) return false;

    uint8_t pkt[sizeof(PtpHeader) + sizeof(PtpTimestamp)] = {};
    auto* hdr = reinterpret_cast<PtpHeader*>(pkt);
    hdr->msgType    = kPtpDelayReq;
    hdr->versionPTP = 0x02;
    hdr->msgLen     = htons(static_cast<uint16_t>(sizeof(pkt)));

    t1Ns = NowNs();

    const int sent = sendto(sock_,
                            reinterpret_cast<const char*>(pkt), static_cast<int>(sizeof(pkt)),
                            0,
                            reinterpret_cast<const sockaddr*>(&addr_), sizeof(addr_));
    return sent > 0;
}

// ────────────────────────────────────────────────────────────────────────────
// ReceiveFollowUp — wait for DELAY_RESP, compute offset
// ────────────────────────────────────────────────────────────────────────────

bool PtpClock::ReceiveFollowUp(int64_t t1Ns)
{
    if (sock_ == INVALID_SOCKET) return false;

    uint8_t buf[256];
    sockaddr_in from{};
    int fromLen = sizeof(from);
    const int bytes = recvfrom(sock_,
                               reinterpret_cast<char*>(buf), sizeof(buf),
                               0,
                               reinterpret_cast<sockaddr*>(&from), &fromLen);

    if (bytes < static_cast<int>(sizeof(PtpHeader) + sizeof(PtpTimestamp)))
        return false;

    const int64_t t4Ns = NowNs();

    const auto* hdr = reinterpret_cast<const PtpHeader*>(buf);
    if ((hdr->msgType & 0x0F) != kPtpDelayResp) return false;

    // Extract device timestamp from DELAY_RESP (t2 = when device received our DELAY_REQ)
    const auto* ts = reinterpret_cast<const PtpTimestamp*>(buf + sizeof(PtpHeader));
    const uint64_t devSec = (static_cast<uint64_t>(ntohs(ts->secondsHi)) << 32) |
                             ntohl(ts->secondsLo);
    const int64_t t2Ns = static_cast<int64_t>(devSec) * 1'000'000'000LL +
                         static_cast<int64_t>(ntohl(ts->nanoseconds));

    // One-way delay estimate: (t4 - t1) / 2
    const int64_t delay = (t4Ns - t1Ns) / 2;

    // Offset: device_time - local_time at the midpoint
    // offset = t2 - (t1 + delay)
    const int64_t offset = t2Ns - (t1Ns + delay);

    // Exponential moving average with α = 0.125
    const int64_t current = clockOffsetNs_.load(std::memory_order_acquire);
    const int64_t updated = current + (offset - current) / 8;
    clockOffsetNs_.store(updated, std::memory_order_release);
    synced_.store(true, std::memory_order_release);

    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// Poll — one sync cycle (called periodically from Thread 5 event loop)
// ────────────────────────────────────────────────────────────────────────────

bool PtpClock::Poll()
{
    if (stop_.load(std::memory_order_acquire) || sock_ == INVALID_SOCKET)
        return false;

    int64_t t1 = 0;
    if (!SendSync(t1))   return sock_ != INVALID_SOCKET;
    ReceiveFollowUp(t1);  // best-effort; failure is non-fatal
    return true;
}

} // namespace AirPlay2
