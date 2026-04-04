// StreamSession.cpp — Concrete implementation for all three pipeline threads.
// All methods called on Thread 1 except the threads themselves.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#pragma comment(lib, "Bcrypt.lib")

#include "core/StreamSession.h"

#include "audio/WasapiCapture.h"
#include "audio/AlacEncoderThread.h"
#include "protocol/RaopSession.h"
#include "protocol/AesCbcCipher.h"
#include "protocol/RetransmitBuffer.h"

// ─────────────────────────────────────────────────────────────────────────────
// Init — Thread 1 (heap alloc OK here)
// ─────────────────────────────────────────────────────────────────────────────

bool StreamSession::Init(const AirPlayReceiver& target, bool lowLatency, HWND hwnd)
{
    target_     = target;
    lowLatency_ = lowLatency;
    hwnd_       = hwnd;

    // Generate fresh AES key and IV for this session
    BCryptGenRandom(nullptr, aesKey_, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    BCryptGenRandom(nullptr, aesIv_,  16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    // Create cipher (pre-init so it's ready before any thread starts)
    cipher_     = std::make_unique<AesCbcCipher>(aesKey_, aesIv_);
    retransmit_ = std::make_unique<RetransmitBuffer>();

    // Allocate ring buffer based on latency mode.
    // SpscRingBuffer is pre-sized here — no resize at runtime (TC-003).
    if (lowLatency) {
        ringLL_  = std::make_unique<SpscRingBuffer<AudioFrame, 32>>();
        ring_    = ringLL_.get();
    } else {
        ringStd_ = std::make_unique<SpscRingBuffer<AudioFrame, 512>>();
        ring_    = ringStd_.get();
    }

    // Default-construct pipeline objects (no threads started yet)
    capture_ = std::make_unique<WasapiCapture>();
    encoder_ = std::make_unique<AlacEncoderThread>();
    raop_    = std::make_unique<RaopSession>();

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Thread 3 (WasapiCapture)
// ─────────────────────────────────────────────────────────────────────────────

bool StreamSession::StartCapture()
{
    return capture_->Start(ring_, hwnd_);
}

void StreamSession::StopCapture()
{
    if (capture_) capture_->Stop();
}

bool StreamSession::ReinitCapture()
{
    if (!capture_) return false;
    capture_->Stop();
    return capture_->Start(ring_, hwnd_);
}

bool StreamSession::IsCaptureRunning() const
{
    return capture_ && capture_->IsRunning();
}

// ─────────────────────────────────────────────────────────────────────────────
// Thread 5 (RaopSession)
// ─────────────────────────────────────────────────────────────────────────────

void StreamSession::StartRaop(float volume)
{
    RaopSession::Config rc;
    rc.receiverIp     = target_.ipAddress;
    rc.receiverPort   = target_.port;
    rc.clientIp       = "0.0.0.0"; // RaopSession resolves actual local IP
    rc.useEncryption  = target_.supportsAes;
    memcpy(rc.aesKey, aesKey_, 16);
    memcpy(rc.aesIv,  aesIv_,  16);
    rc.volume     = volume;
    rc.retransmit = retransmit_.get();
    rc.hwndMain   = hwnd_;

    raop_->Start(rc);
}

void StreamSession::StopRaop()
{
    if (raop_) raop_->Stop();
}

SOCKET StreamSession::AudioSocket() const
{
    return raop_ ? raop_->AudioSocket() : INVALID_SOCKET;
}

// ─────────────────────────────────────────────────────────────────────────────
// Thread 4 (AlacEncoderThread)
// ─────────────────────────────────────────────────────────────────────────────

bool StreamSession::InitEncoder(uint32_t ssrc, HWND hwndMain)
{
    if (!encoder_ || !raop_) return false;

    SOCKET audioSock = raop_->AudioSocket();

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(raop_->ServerAudioPort());
    inet_pton(AF_INET, target_.ipAddress.c_str(), &addr.sin_addr);

    return encoder_->Init(ring_,
                          cipher_.get(),
                          retransmit_.get(),
                          ssrc,
                          audioSock,
                          reinterpret_cast<const sockaddr*>(&addr),
                          static_cast<int>(sizeof(addr)),
                          hwndMain,
                          target_.supportsAes);
}

void StreamSession::StartEncoder()
{
    if (encoder_) encoder_->Start();
}

void StreamSession::StopEncoder()
{
    if (encoder_) encoder_->Stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Volume
// ─────────────────────────────────────────────────────────────────────────────

void StreamSession::SetVolume(float linear)
{
    if (raop_) raop_->SetVolume(linear);
}
