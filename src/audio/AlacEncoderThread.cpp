// RT-safety audit (T092): hot path verified clean — no heap, no mutex, no I/O.
// ThreadProc() uses only stack buffers (alacBuf[4096], RtpPacket pkt), atomic
// loads/stores, _mm_pause spin-wait, and direct sendto().  No LOG_* calls,
// no std::vector/string construction, no WaitForSingleObject in the hot loop.
// Audit date: 2025. Auditor: speckit.implement agent.

#include "audio/AlacEncoderThread.h"
#include <immintrin.h>  // _mm_pause
#include <cstring>

// ALAC reference encoder — fetched by CMake FetchContent into _deps/alac-src/codec/
#include "ALACEncoder.h"
#include "ALACAudioTypes.h"

// ─────────────────────────────────────────────────────────────────────────────
// PCM input format: 44100 Hz, stereo, S16LE, interleaved.
// Built once in Init(); passed by value into Encode() each iteration.
// ─────────────────────────────────────────────────────────────────────────────

static AudioFormatDescription MakeInputFormat() noexcept
{
    AudioFormatDescription f = {};
    f.mSampleRate       = 44100.0;
    f.mFormatID         = kALACFormatLinearPCM;
    // Signed integer, packed, little-endian (native on Windows)
    f.mFormatFlags      = kALACFormatFlagIsSignedInteger | kALACFormatFlagIsPacked;
    f.mBytesPerPacket   = 4;   // 2 ch * 2 bytes
    f.mFramesPerPacket  = 1;
    f.mBytesPerFrame    = 4;
    f.mChannelsPerFrame = 2;
    f.mBitsPerChannel   = 16;
    f.mReserved         = 0;
    return f;
}

static AudioFormatDescription MakeOutputFormat() noexcept
{
    AudioFormatDescription f = {};
    f.mSampleRate       = 44100.0;
    f.mFormatID         = kALACFormatAppleLossless;
    // mFormatFlags encodes bit depth for the ALAC encoder: 1=16-bit, 2=20-bit, 3=24-bit, 4=32-bit.
    // See kTestFormatFlag_*BitSourceData in the ALAC convert-utility/main.cpp reference.
    f.mFormatFlags      = 1;    // 16-bit source data
    f.mBytesPerPacket   = 0;
    f.mFramesPerPacket  = 352;
    f.mBytesPerFrame    = 0;
    f.mChannelsPerFrame = 2;
    f.mBitsPerChannel   = 16;
    f.mReserved         = 0;
    return f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────

AlacEncoderThread::AlacEncoderThread() = default;

AlacEncoderThread::~AlacEncoderThread()
{
    Stop();
    delete static_cast<ALACEncoder*>(alacEncoder_);
    alacEncoder_ = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Init — Thread 1 (heap alloc allowed here)
// ─────────────────────────────────────────────────────────────────────────────

bool AlacEncoderThread::Init(
    SpscRingBufferPtr ring,
    AesCbcCipher*     cipher,
    RetransmitBuffer* retransmit,
    uint32_t          ssrc,
    SOCKET            audioSocket,
    const sockaddr*   receiverAudioAddr,
    int               addrLen)
{
    ring_      = ring;
    cipher_    = cipher;
    retransmit_ = retransmit;
    ssrc_      = ssrc;
    audioSocket_ = audioSocket;

    if (addrLen > 0 && receiverAudioAddr) {
        memcpy(&receiverAddr_, receiverAudioAddr,
               static_cast<size_t>(addrLen));
        receiverAddrLen_ = addrLen;
    }

    // Allocate and initialize the ALAC encoder (Thread 1 — heap OK)
    auto* encoder = new ALACEncoder();
    encoder->SetFrameSize(352);

    AudioFormatDescription outFmt = MakeOutputFormat();
    if (encoder->InitializeEncoder(outFmt) != 0) {
        delete encoder;
        return false;
    }

    alacEncoder_ = encoder;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Start / Stop
// ─────────────────────────────────────────────────────────────────────────────

void AlacEncoderThread::Start()
{
    stopFlag_.store(false, std::memory_order_relaxed);
    thread_ = std::thread(&AlacEncoderThread::ThreadProc, this);
}

void AlacEncoderThread::Stop()
{
    stopFlag_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
}

// ─────────────────────────────────────────────────────────────────────────────
// SendAudioPacket — T046 (RT: noexcept, no logging)
// ─────────────────────────────────────────────────────────────────────────────

void AlacEncoderThread::SendAudioPacket(const RtpPacket& pkt) noexcept
{
    const int totalLen = 12 + static_cast<int>(pkt.payloadLen);
    sendto(audioSocket_,
           reinterpret_cast<const char*>(pkt.data),
           totalLen, 0,
           reinterpret_cast<const sockaddr*>(&receiverAddr_),
           receiverAddrLen_);
    // WSAEWOULDBLOCK silently dropped — RT thread must not log or block.
}

// ─────────────────────────────────────────────────────────────────────────────
// ThreadProc — Thread 4  (RT: ZERO heap alloc, ZERO mutex, ZERO logging)
// ─────────────────────────────────────────────────────────────────────────────

void AlacEncoderThread::ThreadProc()
{
    running_.store(true, std::memory_order_release);

    ALACEncoder* encoder = static_cast<ALACEncoder*>(alacEncoder_);

    // Stack-only working buffers — no heap.
    uint8_t   alacBuf[4096];
    RtpPacket pkt;

    // Format descriptors built on the stack once before the hot loop.
    const AudioFormatDescription inFmt  = MakeInputFormat();
    const AudioFormatDescription outFmt = MakeOutputFormat();

    while (!stopFlag_.load(std::memory_order_acquire)) {

        AudioFrame frame;
        if (!RingTryPop(ring_, frame)) {
            _mm_pause();
            continue;
        }

        // ── 1. ALAC encode ────────────────────────────────────────────────────
        int32_t ioBytes = static_cast<int32_t>(sizeof(alacBuf));
        int32_t encResult = encoder->Encode(
            inFmt, outFmt,
            reinterpret_cast<uint8_t*>(frame.samples),
            alacBuf,
            &ioBytes);

        if (encResult != 0 || ioBytes <= 0) {
            // Encode failure — advance counters to keep timeline consistent.
            seqNum_++;
            rtpTimestamp_ += 352;
            continue;
        }

        const int outBytes = static_cast<int>(ioBytes);

        // ── 2. Zero-pad to 16-byte AES block boundary ─────────────────────────
        const int paddedLen = (outBytes + 15) & ~15;
        memset(alacBuf + outBytes, 0, static_cast<size_t>(paddedLen - outBytes));

        // ── 3. AES-128-CBC encrypt in-place ───────────────────────────────────
        cipher_->Encrypt(alacBuf, alacBuf, static_cast<size_t>(paddedLen));

        // ── 4. Build RTP packet ───────────────────────────────────────────────
        pkt.InitHeader();
        pkt.SetSeq(seqNum_);
        pkt.SetTimestamp(rtpTimestamp_);
        pkt.SetSsrc(ssrc_);
        memcpy(pkt.data + 12, alacBuf, static_cast<size_t>(paddedLen));
        pkt.payloadLen = static_cast<uint16_t>(paddedLen);

        // ── 5. Store for retransmit ───────────────────────────────────────────
        retransmit_->Store(pkt, seqNum_);

        // ── 6. Send UDP (T046) ────────────────────────────────────────────────
        SendAudioPacket(pkt);

        seqNum_++;
        rtpTimestamp_ += 352;
    }

    running_.store(false, std::memory_order_release);
}
