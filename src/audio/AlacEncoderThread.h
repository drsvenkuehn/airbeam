#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <atomic>
#include <thread>
#include <cstdint>
#include "audio/SpscRingBuffer.h"
#include "protocol/AesCbcCipher.h"
#include "protocol/RetransmitBuffer.h"
#include "protocol/RtpPacket.h"

class AlacEncoderThread {
public:
    AlacEncoderThread();
    ~AlacEncoderThread();

    AlacEncoderThread(const AlacEncoderThread&) = delete;
    AlacEncoderThread& operator=(const AlacEncoderThread&) = delete;

    /// Initialize ALAC encoder state on Thread 1 (heap alloc allowed here).
    /// Must be called before Start().
    /// Returns false if ALAC init fails.
    bool Init(SpscRingBufferPtr ring,
              AesCbcCipher*     cipher,      // owned by ConnectionController
              RetransmitBuffer* retransmit,  // owned by ConnectionController
              uint32_t          ssrc,
              SOCKET            audioSocket,
              const sockaddr*   receiverAudioAddr,
              int               addrLen);

    /// Start Thread 4.
    void Start();

    /// Signal stop and join (max 200 ms).
    void Stop();

    bool IsRunning() const { return running_.load(std::memory_order_acquire); }

private:
    void ThreadProc();
    void SendAudioPacket(const RtpPacket& pkt) noexcept;

    SpscRingBufferPtr   ring_{static_cast<SpscRingBuffer<AudioFrame,128>*>(nullptr)};
    AesCbcCipher*       cipher_          = nullptr;
    RetransmitBuffer*   retransmit_      = nullptr;
    void*               alacEncoder_     = nullptr;  // opaque ALACEncoder*
    uint32_t            ssrc_            = 0;
    uint16_t            seqNum_          = 0;
    uint32_t            rtpTimestamp_    = 0;

    // T046: UDP audio socket (fd passed from RaopSession after SETUP)
    SOCKET              audioSocket_     = INVALID_SOCKET;
    sockaddr_storage    receiverAddr_    = {};
    int                 receiverAddrLen_ = 0;

    std::thread         thread_;
    std::atomic<bool>   stopFlag_{false};
    std::atomic<bool>   running_{false};
};
