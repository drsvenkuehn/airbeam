#pragma once
#include <cstdint>

/// One ALAC frame worth of audio: 352 stereo PCM-16 samples.
/// Interleaved: [L0, R0, L1, R1, ..., L351, R351]
/// Total: 704 int16_t values = 1408 bytes.
struct AudioFrame {
    int16_t  samples[704];   ///< 352 stereo samples (interleaved L/R)
    uint32_t frameCount;     ///< sequence number assigned by WasapiCapture
};

static_assert(sizeof(AudioFrame) == 1412, "AudioFrame size unexpected");
