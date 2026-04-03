#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

#include "protocol/RaopSession.h"

#include <bcrypt.h>

#include "protocol/NtpClock.h"
#include "protocol/SdpBuilder.h"
#include "protocol/RsaKeyWrap.h"
#include "protocol/VolumeMapper.h"
#include "core/Messages.h"
#include "core/Logger.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

// ---- file-scope helpers --------------------------------------------------

static std::string Base64Encode(const uint8_t* data, size_t len)
{
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) b |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) b |= static_cast<uint32_t>(data[i + 2]);

        out += kAlphabet[(b >> 18) & 0x3F];
        out += kAlphabet[(b >> 12) & 0x3F];
        out += (i + 1 < len) ? kAlphabet[(b >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? kAlphabet[b & 0x3F]        : '=';
    }
    return out;
}

static void StoreU32BE(uint8_t* p, uint32_t v) noexcept
{
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >>  8);
    p[3] = static_cast<uint8_t>(v);
}

static void StoreU64BE(uint8_t* p, uint64_t v) noexcept
{
    StoreU32BE(p,     static_cast<uint32_t>(v >> 32));
    StoreU32BE(p + 4, static_cast<uint32_t>(v));
}

static uint16_t LoadU16BE(const uint8_t* p) noexcept
{
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

/// Parses a semicolon-delimited key=value from an RTSP Transport header value.
static uint16_t ParseTransportPort(const std::string& transport, const std::string& key)
{
    auto pos = transport.find(key + "=");
    if (pos == std::string::npos) return 0;
    pos += key.size() + 1;
    auto end = transport.find_first_of(";\r\n ", pos);
    std::string val = (end == std::string::npos)
        ? transport.substr(pos)
        : transport.substr(pos, end - pos);
    if (val.empty()) return 0;
    try { return static_cast<uint16_t>(std::stoi(val)); }
    catch (...) { return 0; }
}

/// Binds a UDP socket on an ephemeral port and returns the assigned port.
static bool BindUdp(SOCKET& sockOut, uint16_t& portOut)
{
    sockOut = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockOut == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = 0;

    if (bind(sockOut, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sockOut);
        sockOut = INVALID_SOCKET;
        return false;
    }

    sockaddr_in bound{};
    int boundLen = sizeof(bound);
    getsockname(sockOut, reinterpret_cast<sockaddr*>(&bound), &boundLen);
    portOut = ntohs(bound.sin_port);
    return true;
}

// ---- RaopSession ctor / dtor ---------------------------------------------

RaopSession::RaopSession() = default;

RaopSession::~RaopSession()
{
    Stop();
}

// ---- Public API ----------------------------------------------------------

void RaopSession::Start(const Config& cfg)
{
    cfg_ = cfg;
    stopFlag_.store(false, std::memory_order_release);
    thread_ = std::thread(&RaopSession::ThreadProc, this);
}

void RaopSession::Stop()
{
    stopFlag_.store(true, std::memory_order_release);
    if (thread_.joinable())
        thread_.join();
}

void RaopSession::SetVolume(float linear)
{
    pendingVolume_.store(linear, std::memory_order_release);
}

// ---- ThreadProc ----------------------------------------------------------

void RaopSession::ThreadProc()
{
    running_.store(true, std::memory_order_release);

    WSADATA wd{};
    WSAStartup(MAKEWORD(2, 2), &wd);

    auto closeAll = [&]() {
        if (tcpSock_       != INVALID_SOCKET) { closesocket(tcpSock_);       tcpSock_       = INVALID_SOCKET; }
        if (audioSocket_   != INVALID_SOCKET) { closesocket(audioSocket_);   audioSocket_   = INVALID_SOCKET; }
        if (controlSocket_ != INVALID_SOCKET) { closesocket(controlSocket_); controlSocket_ = INVALID_SOCKET; }
        if (timingSocket_  != INVALID_SOCKET) { closesocket(timingSocket_);  timingSocket_  = INVALID_SOCKET; }
    };

    bool connected = false;
    for (int attempt = 0; attempt < 3 && !stopFlag_.load(std::memory_order_acquire); ++attempt) {
        if (DoConnect()) {
            connected = true;
            PostMessage(cfg_.hwndMain, WM_RAOP_CONNECTED, 0, 0);
            EventLoop();  // blocks until stopFlag_ or connection error

            // Send TEARDOWN; allow 1 s for the receiver to respond
            SendRtsp(BuildTeardown());
            DWORD toMs = 1000;
            setsockopt(tcpSock_, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&toMs), sizeof(toMs));
            RecvRtspResponse();
            break;
        }

        closeAll();

        if (attempt < 2 && !stopFlag_.load(std::memory_order_acquire)) {
            DWORD backoff = (1u << static_cast<unsigned>(attempt)) * 1000u;
            Sleep(backoff);
        }
    }

    closeAll();

    if (!connected)
        PostMessage(cfg_.hwndMain, WM_RAOP_FAILED, 0, 0);

    WSACleanup();
    running_.store(false, std::memory_order_release);
}

// ---- DoConnect: OPTIONS → ANNOUNCE → SETUP → RECORD ---------------------

bool RaopSession::DoConnect()
{
    // 1. Generate per-session identifiers ----------------------------------------

    {
        uint8_t rnd[8] = {};
        BCryptGenRandom(nullptr, rnd, 8, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        char hex[17];
        snprintf(hex, sizeof(hex), "%02x%02x%02x%02x%02x%02x%02x%02x",
                 rnd[0], rnd[1], rnd[2], rnd[3],
                 rnd[4], rnd[5], rnd[6], rnd[7]);
        clientInstance_ = hex;
    }

    // DACP-ID: 16 uppercase hex chars; Active-Remote: random 32-bit decimal
    // Required by many modern AirTunes receivers (JBL, Samsung, etc.)
    {
        uint8_t rnd[8] = {};
        BCryptGenRandom(nullptr, rnd, 8, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        char hex[17];
        snprintf(hex, sizeof(hex), "%02X%02X%02X%02X%02X%02X%02X%02X",
                 rnd[0], rnd[1], rnd[2], rnd[3],
                 rnd[4], rnd[5], rnd[6], rnd[7]);
        dacpId_ = hex;

        uint32_t ar = 0;
        BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&ar), 4, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        activeRemote_ = std::to_string(ar);
    }

    sessionId_ = NtpClock::NowNtp64();

    {
        std::ostringstream oss;
        oss << "rtsp://" << cfg_.receiverIp << ":"
            << cfg_.receiverPort << "/" << sessionId_;
        rtspUrl_ = oss.str();
    }

    // 2. Pre-compute crypto material for ANNOUNCE --------------------------------

    if (cfg_.useEncryption) {
        rsaAesKey_b64_ = RsaKeyWrap::Wrap(cfg_.aesKey);
        if (rsaAesKey_b64_.empty()) return false;
        aesIv_b64_ = Base64Encode(cfg_.aesIv, 16);
    }

    LOG_DEBUG("RAOP: useEncryption=%d clientIp=%s", cfg_.useEncryption ? 1 : 0, cfg_.clientIp.c_str());

    cseq_ = 1;
    sessionToken_.clear();

    // 3. TCP connect with 3-second timeout ---------------------------------------

    tcpSock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcpSock_ == INVALID_SOCKET) return false;

    // Switch to non-blocking for the timed connect
    u_long nonblock = 1;
    ioctlsocket(tcpSock_, FIONBIO, &nonblock);

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(cfg_.receiverPort);
    inet_pton(AF_INET, cfg_.receiverIp.c_str(), &sa.sin_addr);

    int ret = connect(tcpSock_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    if (ret == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(tcpSock_);
        tcpSock_ = INVALID_SOCKET;
        return false;
    }

    {
        fd_set wfds, efds;
        FD_ZERO(&wfds); FD_SET(tcpSock_, &wfds);
        FD_ZERO(&efds); FD_SET(tcpSock_, &efds);
        timeval tv{ 3, 0 };
        ret = select(0, nullptr, &wfds, &efds, &tv);
        if (ret <= 0 || FD_ISSET(tcpSock_, &efds)) {
            closesocket(tcpSock_);
            tcpSock_ = INVALID_SOCKET;
            return false;
        }
        int err = 0; int errLen = sizeof(err);
        getsockopt(tcpSock_, SOL_SOCKET, SO_ERROR,
                   reinterpret_cast<char*>(&err), &errLen);
        if (err != 0) {
            closesocket(tcpSock_);
            tcpSock_ = INVALID_SOCKET;
            return false;
        }
    }

    // Back to blocking with a 5-second receive timeout
    nonblock = 0;
    ioctlsocket(tcpSock_, FIONBIO, &nonblock);
    DWORD rcvTimeout = 5000;
    setsockopt(tcpSock_, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));

    // Resolve the actual local IP that was assigned by the OS for this connection
    {
        sockaddr_in localAddr{};
        int localAddrLen = sizeof(localAddr);
        if (getsockname(tcpSock_, reinterpret_cast<sockaddr*>(&localAddr), &localAddrLen) == 0) {
            char ipBuf[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &localAddr.sin_addr, ipBuf, sizeof(ipBuf));
            cfg_.clientIp = ipBuf;
        }
    }

    // 4. OPTIONS -----------------------------------------------------------------

    LOG_DEBUG("RAOP: sending OPTIONS to %s:%u", cfg_.receiverIp.c_str(), cfg_.receiverPort);
    std::string optionsMsg = BuildOptions();
    if (!SendRtsp(optionsMsg)) { LOG_DEBUG("RAOP: OPTIONS send failed"); return false; }
    {
        std::string r = RecvRtspResponse();
        int code = ParseStatusCode(r);
        LOG_DEBUG("RAOP: OPTIONS response %d", code);
        if (code != 200) return false;
        // Log the full OPTIONS response so we can see Apple-Challenge etc.
        LOG_DEBUG("RAOP: OPTIONS resp body:\n%s", r.c_str());
    }

    // 5. ANNOUNCE ----------------------------------------------------------------

    std::string announceMsg = BuildAnnounce();
    LOG_DEBUG("RAOP: sending ANNOUNCE (len=%zu useEncryption=%d)\n%s",
              announceMsg.size(), cfg_.useEncryption ? 1 : 0, announceMsg.c_str());
    if (!SendRtsp(announceMsg)) { LOG_DEBUG("RAOP: ANNOUNCE send failed"); return false; }
    {
        std::string r = RecvRtspResponse();
        int code = ParseStatusCode(r);
        LOG_DEBUG("RAOP: ANNOUNCE response %d body:\n%s", code, r.c_str());
        if (code != 200) return false;
    }

    // 6. Bind UDP sockets (ephemeral ports) -------------------------------------

    if (!BindUdp(audioSocket_,   localAudioPort_))   return false;
    if (!BindUdp(controlSocket_, localControlPort_)) return false;
    if (!BindUdp(timingSocket_,  localTimingPort_))  return false;

    // 7. SETUP -------------------------------------------------------------------

    LOG_DEBUG("RAOP: sending SETUP (audio=%u control=%u timing=%u)",
              localAudioPort_, localControlPort_, localTimingPort_);
    if (!SendRtsp(BuildSetup())) { LOG_DEBUG("RAOP: SETUP send failed"); return false; }
    std::string setupResp = RecvRtspResponse();
    {
        int code = ParseStatusCode(setupResp);
        LOG_DEBUG("RAOP: SETUP response %d", code);
        if (code != 200) return false;
    }

    // Extract session token (strip optional ;timeout=N suffix and whitespace)
    sessionToken_ = ExtractHeader(setupResp, "Session");
    {
        auto semi = sessionToken_.find(';');
        if (semi != std::string::npos)
            sessionToken_ = sessionToken_.substr(0, semi);
        while (!sessionToken_.empty() && sessionToken_.front() == ' ')
            sessionToken_.erase(sessionToken_.begin());
        while (!sessionToken_.empty() && sessionToken_.back() == ' ')
            sessionToken_.pop_back();
    }

    // Extract server UDP ports from Transport header
    {
        std::string transport = ExtractHeader(setupResp, "Transport");
        serverAudioPort_   = ParseTransportPort(transport, "server_port");
        serverControlPort_ = ParseTransportPort(transport, "control_port");
        serverTimingPort_  = ParseTransportPort(transport, "timing_port");
        if (serverAudioPort_ == 0) return false;
    }

    // 8. RECORD ------------------------------------------------------------------

    LOG_DEBUG("RAOP: sending RECORD (serverAudio=%u ctrl=%u timing=%u)",
              serverAudioPort_, serverControlPort_, serverTimingPort_);
    if (!SendRtsp(BuildRecord())) { LOG_DEBUG("RAOP: RECORD send failed"); return false; }
    {
        std::string r = RecvRtspResponse();
        int code = ParseStatusCode(r);
        LOG_DEBUG("RAOP: RECORD response %d", code);
        if (code != 200) return false;
    }

    LOG_DEBUG("RAOP: handshake complete — streaming");

    // 9. Set up the target address used for all UDP sends -----------------------

    receiverAddr_             = {};
    receiverAddr_.sin_family  = AF_INET;
    inet_pton(AF_INET, cfg_.receiverIp.c_str(), &receiverAddr_.sin_addr);

    lastSyncRtpTs_ = 0;
    rtpPacketCount_ = 0;

    return true;
}

// ---- RTSP send / receive -------------------------------------------------

bool RaopSession::SendRtsp(const std::string& msg)
{
    const char* p   = msg.c_str();
    int          rem = static_cast<int>(msg.size());
    while (rem > 0) {
        int n = send(tcpSock_, p, rem, 0);
        if (n == SOCKET_ERROR) return false;
        p   += n;
        rem -= n;
    }
    return true;
}

std::string RaopSession::RecvRtspResponse()
{
    std::string response;
    char buf[4096];

    while (true) {
        int n = recv(tcpSock_, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        response.append(buf, n);

        auto hdrEnd = response.find("\r\n\r\n");
        if (hdrEnd == std::string::npos) continue;

        // If there is a body, read it in full
        std::string cl = ExtractHeader(response, "Content-Length");
        if (cl.empty()) break;
        int contentLen = 0;
        try { contentLen = std::stoi(cl); } catch (...) { break; }
        if (contentLen <= 0) break;

        size_t bodyStart = hdrEnd + 4;
        while (static_cast<int>(response.size() - bodyStart) < contentLen) {
            n = recv(tcpSock_, buf, sizeof(buf) - 1, 0);
            if (n <= 0) break;
            response.append(buf, n);
        }
        break;
    }
    return response;
}

int RaopSession::ParseStatusCode(const std::string& response)
{
    auto pos = response.find("RTSP/1.0 ");
    if (pos == std::string::npos) return -1;
    pos += 9;
    if (pos + 3 > response.size()) return -1;
    try { return std::stoi(response.substr(pos, 3)); }
    catch (...) { return -1; }
}

std::string RaopSession::ExtractHeader(const std::string& response,
                                       const std::string& name)
{
    std::string needle = "\r\n" + name + ":";
    auto pos = response.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();

    // Skip optional leading whitespace
    while (pos < response.size() && response[pos] == ' ')
        ++pos;

    auto end = response.find("\r\n", pos);
    if (end == std::string::npos) end = response.size();
    return response.substr(pos, end - pos);
}

// ---- RTSP message builders -----------------------------------------------

std::string RaopSession::BuildOptions()
{
    std::ostringstream oss;
    oss << "OPTIONS * RTSP/1.0\r\n"
        << "CSeq: "            << cseq_++         << "\r\n"
        << "User-Agent: AirBeam/1.0\r\n"
        << "Client-Instance: " << clientInstance_ << "\r\n"
        << "DACP-ID: "         << dacpId_         << "\r\n"
        << "Active-Remote: "   << activeRemote_   << "\r\n"
        << "\r\n";
    return oss.str();
}

std::string RaopSession::BuildAnnounce()
{
    std::string sdp = SdpBuilder::Build(
        cfg_.clientIp,
        cfg_.receiverIp,
        sessionId_,
        rsaAesKey_b64_,
        aesIv_b64_,
        cfg_.useEncryption);   // skip a=rsaaeskey/a=aesiv for et=0 devices

    std::ostringstream oss;
    oss << "ANNOUNCE " << rtspUrl_ << " RTSP/1.0\r\n"
        << "CSeq: "             << cseq_++         << "\r\n"
        << "User-Agent: AirBeam/1.0\r\n"
        << "Client-Instance: "  << clientInstance_ << "\r\n"
        << "DACP-ID: "          << dacpId_         << "\r\n"
        << "Active-Remote: "    << activeRemote_   << "\r\n"
        << "Content-Type: application/sdp\r\n"
        << "Content-Length: "   << sdp.size()      << "\r\n"
        << "\r\n"
        << sdp;
    return oss.str();
}

std::string RaopSession::BuildSetup()
{
    std::ostringstream oss;
    oss << "SETUP " << rtspUrl_ << " RTSP/1.0\r\n"
        << "CSeq: "            << cseq_++         << "\r\n"
        << "User-Agent: AirBeam/1.0\r\n"
        << "Client-Instance: " << clientInstance_ << "\r\n"
        << "Transport: RTP/AVP/UDP;unicast;interleaved=0-1;mode=record"
        << ";control_port=" << localControlPort_
        << ";timing_port="  << localTimingPort_
        << "\r\n"
        << "\r\n";
    return oss.str();
}

std::string RaopSession::BuildRecord()
{
    std::ostringstream oss;
    oss << "RECORD " << rtspUrl_ << " RTSP/1.0\r\n"
        << "CSeq: "            << cseq_++         << "\r\n"
        << "User-Agent: AirBeam/1.0\r\n"
        << "Client-Instance: " << clientInstance_ << "\r\n"
        << "Session: "         << sessionToken_   << "\r\n"
        << "RTP-Info: seq=0;rtptime=0\r\n"
        << "\r\n";
    return oss.str();
}

std::string RaopSession::BuildTeardown()
{
    std::ostringstream oss;
    oss << "TEARDOWN " << rtspUrl_ << " RTSP/1.0\r\n"
        << "CSeq: "    << cseq_++      << "\r\n"
        << "Session: " << sessionToken_ << "\r\n"
        << "\r\n";
    return oss.str();
}

std::string RaopSession::BuildSetParameter(float linear)
{
    float db = VolumeMapper::LinearToDb(linear);
    char dbStr[32];
    snprintf(dbStr, sizeof(dbStr), "%.6f", db);
    std::string body = std::string("volume: ") + dbStr + "\r\n";

    std::ostringstream oss;
    oss << "SET_PARAMETER " << rtspUrl_ << " RTSP/1.0\r\n"
        << "CSeq: "    << cseq_++          << "\r\n"
        << "Session: " << sessionToken_    << "\r\n"
        << "Content-Type: text/parameters\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

// ---- EventLoop -----------------------------------------------------------

void RaopSession::EventLoop()
{
    ULONGLONG lastNtpSyncMs = GetTickCount64();

    while (!stopFlag_.load(std::memory_order_acquire)) {
        // Apply any pending volume change synchronously before select()
        {
            float vol = pendingVolume_.load(std::memory_order_acquire);
            if (vol >= 0.0f) {
                if (SendRtsp(BuildSetParameter(vol)))
                    RecvRtspResponse(); // drain the 200 OK
                pendingVolume_.store(-1.0f, std::memory_order_release);
            }
        }

        // Send NTP sync every ~100 ms
        {
            ULONGLONG now = GetTickCount64();
            if (now - lastNtpSyncMs >= 100) {
                SendNtpSync();
                lastNtpSyncMs = now;
            }
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(tcpSock_,       &rfds);
        FD_SET(controlSocket_, &rfds);
        FD_SET(timingSocket_,  &rfds);

        timeval tv{ 0, 50000 }; // 50 ms
        int ret = select(0, &rfds, nullptr, nullptr, &tv);
        if (ret == SOCKET_ERROR) break;
        if (ret == 0) continue;

        // TCP: detect remote close or unexpected disconnect
        if (FD_ISSET(tcpSock_, &rfds)) {
            char dummy[256];
            int n = recv(tcpSock_, dummy, sizeof(dummy), 0);
            if (n <= 0) break;
        }

        // Timing socket: NTP timing exchange with the receiver
        if (FD_ISSET(timingSocket_, &rfds)) {
            uint8_t buf[64];
            sockaddr_in from{};
            int fromLen = sizeof(from);
            int n = recvfrom(timingSocket_,
                             reinterpret_cast<char*>(buf), sizeof(buf), 0,
                             reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n >= 32)
                HandleTimingRequest(buf, n, from);
        }

        // Control socket: retransmit requests and other control packets
        if (FD_ISSET(controlSocket_, &rfds)) {
            uint8_t buf[256];
            sockaddr_in from{};
            int fromLen = sizeof(from);
            int n = recvfrom(controlSocket_,
                             reinterpret_cast<char*>(buf), sizeof(buf), 0,
                             reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n > 0)
                HandleControlPacket(buf, n);
        }
    }
}

// ---- HandleTimingRequest -------------------------------------------------

void RaopSession::HandleTimingRequest(const uint8_t* buf, int len,
                                      const sockaddr_in& from)
{
    if (len < 32) return;

    // Response layout (32 bytes):
    //   [0]    0x80
    //   [1]    0xD3  timing response
    //   [2-3]  0x0007
    //   [4-7]  0x00000000
    //   [8-15] originate timestamp (copied verbatim from request bytes 8-15)
    //   [16-23] receive timestamp  = NtpClock::NowNtp64()
    //   [24-31] transmit timestamp = NtpClock::NowNtp64()
    uint8_t resp[32] = {};
    resp[0] = 0x80;
    resp[1] = 0xD3;
    resp[2] = 0x00;
    resp[3] = 0x07;
    // bytes 4-7 stay zero
    std::memcpy(resp + 8, buf + 8, 8);
    StoreU64BE(resp + 16, NtpClock::NowNtp64());
    StoreU64BE(resp + 24, NtpClock::NowNtp64());

    sendto(timingSocket_,
           reinterpret_cast<const char*>(resp), sizeof(resp), 0,
           reinterpret_cast<const sockaddr*>(&from), sizeof(from));
}

// ---- SendNtpSync ---------------------------------------------------------

void RaopSession::SendNtpSync()
{
    // 16-byte AirPlay sync packet:
    //   [0]    0x80  (V=2)
    //   [1]    0xD4  (marker=1, PT=0x54)
    //   [2-3]  0x0007
    //   [4-7]  current RTP timestamp (BE)
    //   [8-15] NTP now (BE)
    uint8_t pkt[16] = {};
    pkt[0] = 0x80;
    pkt[1] = 0xD4;
    pkt[2] = 0x00;
    pkt[3] = 0x07;
    StoreU32BE(pkt + 4, lastSyncRtpTs_);
    StoreU64BE(pkt + 8, NtpClock::NowNtp64());

    lastSyncRtpTs_ += kNtpSyncInterval;

    sockaddr_in dest  = receiverAddr_;
    dest.sin_port     = htons(serverControlPort_);
    sendto(controlSocket_,
           reinterpret_cast<const char*>(pkt), sizeof(pkt), 0,
           reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
}

// ---- HandleControlPacket -------------------------------------------------

void RaopSession::HandleControlPacket(const uint8_t* buf, int len)
{
    if (len < 8) return;

    // Payload type lives in the lower 7 bits of byte 1
    uint8_t pt = buf[1] & 0x7F;

    if (pt == 0x55) {
        // Retransmit request: bytes 4-5 = first seq (BE), bytes 6-7 = count (BE)
        uint16_t firstSeq = LoadU16BE(buf + 4);
        uint16_t count    = LoadU16BE(buf + 6);

        if (!cfg_.retransmit) return;

        sockaddr_in dest = receiverAddr_;
        dest.sin_port    = htons(serverAudioPort_);

        for (uint16_t i = 0; i < count; ++i) {
            uint16_t seq = static_cast<uint16_t>(firstSeq + i);
            const RtpPacket* pkt = cfg_.retransmit->Retrieve(seq);
            if (pkt) {
                sendto(audioSocket_,
                       reinterpret_cast<const char*>(pkt->data),
                       12 + pkt->payloadLen, 0,
                       reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
            }
        }
    }
}
