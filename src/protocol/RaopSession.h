#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <atomic>
#include <thread>
#include <cstdint>
#include "protocol/RetransmitBuffer.h"

class AesCbcCipher;  // forward declare

/// Manages one RAOP session on Thread 5:
///   RTSP TCP control + timing UDP + control UDP + retransmit handler.
/// Exposes the audio UDP socket (bound during SETUP) for Thread 4 to use.
class RaopSession {
public:
    struct Config {
        std::string  receiverIp;
        uint16_t     receiverPort    = 5000;
        std::string  clientIp;           // local IP for SDP
        uint8_t      aesKey[16]      = {};
        uint8_t      aesIv[16]       = {};
        float        volume          = 1.0f;  // linear [0,1]
        bool         useEncryption   = true;  // false for et=0 devices (no RSA-AES)
        RetransmitBuffer* retransmit = nullptr;
        HWND         hwndMain        = nullptr;
    };

    RaopSession();
    ~RaopSession();

    RaopSession(const RaopSession&)            = delete;
    RaopSession& operator=(const RaopSession&) = delete;

    /// Starts Thread 5. Initiates RTSP OPTIONS→ANNOUNCE→SETUP→RECORD sequence.
    /// On success posts WM_RAOP_CONNECTED. On failure posts WM_RAOP_FAILED.
    /// Retries 3× with exponential backoff (1s, 2s, 4s).
    void Start(const Config& cfg);

    /// Signals Thread 5 to teardown (sends RTSP TEARDOWN) and joins.
    void Stop();

    /// Called by AppController to change volume mid-session.
    /// Sends RTSP SET_PARAMETER on Thread 5 (thread-safe via atomic).
    void SetVolume(float linear);

    /// Returns the audio UDP socket after SETUP (INVALID_SOCKET until then).
    /// Thread 4 reads this once after WM_RAOP_CONNECTED.
    SOCKET AudioSocket() const { return audioSocket_; }

    /// Returns the server's audio UDP port (from SETUP response).
    uint16_t ServerAudioPort() const { return serverAudioPort_; }

    bool IsRunning() const { return running_.load(std::memory_order_acquire); }

private:
    void   ThreadProc();
    bool   DoConnect();         // OPTIONS→ANNOUNCE→SETUP→RECORD, returns true on success
    bool   SendRtsp(const std::string& msg);
    std::string RecvRtspResponse();
    int    ParseStatusCode(const std::string& response);
    std::string ExtractHeader(const std::string& response, const std::string& name);

    // RTSP message builders
    std::string BuildOptions();
    std::string BuildAnnounce();
    std::string BuildSetup();
    std::string BuildRecord();
    std::string BuildTeardown();
    std::string BuildSetParameter(float linear);

    // Event loop: select() on TCP + control + timing sockets
    void EventLoop();
    void HandleTimingRequest(const uint8_t* buf, int len, const sockaddr_in& from);
    void HandleControlPacket(const uint8_t* buf, int len);
    void SendNtpSync();

    Config            cfg_;

    // RTSP TCP socket
    SOCKET            tcpSock_         = INVALID_SOCKET;
    int               cseq_            = 1;
    std::string       sessionToken_;
    std::string       clientInstance_; // 16 random hex chars
    std::string       dacpId_;         // 16 random uppercase hex chars (DACP session ID)
    std::string       activeRemote_;   // random decimal (DACP active remote token)

    // Session identity (generated once per DoConnect)
    uint64_t          sessionId_       = 0;
    std::string       rtspUrl_;
    std::string       rsaAesKey_b64_;
    std::string       aesIv_b64_;

    // UDP sockets (bound locally, target determined from SETUP response)
    SOCKET            audioSocket_     = INVALID_SOCKET;  // Thread 4 reads this
    SOCKET            controlSocket_   = INVALID_SOCKET;  // Thread 5 owns
    SOCKET            timingSocket_    = INVALID_SOCKET;  // Thread 5 owns

    uint16_t          localAudioPort_  = 0;
    uint16_t          localControlPort_= 0;
    uint16_t          localTimingPort_ = 0;

    uint16_t          serverAudioPort_    = 0;
    uint16_t          serverControlPort_  = 0;
    uint16_t          serverTimingPort_   = 0;

    sockaddr_in       receiverAddr_    = {};  // for UDP sends

    // NTP sync state
    uint32_t          rtpPacketCount_  = 0;
    uint32_t          lastSyncRtpTs_   = 0;
    static constexpr uint32_t kNtpSyncInterval = 4410; // every 100ms @ 44100 Hz

    // Volume (set atomically from Thread 1, applied on Thread 5)
    std::atomic<float> pendingVolume_{-1.0f}; // -1 = no pending change

    std::thread       thread_;
    std::atomic<bool> stopFlag_{false};
    std::atomic<bool> running_{false};
};
