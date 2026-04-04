#pragma once

#include <stdint.h>

typedef int16_t  spx_int16_t;
typedef uint16_t spx_uint16_t;
typedef int32_t  spx_int32_t;
typedef uint32_t spx_uint32_t;

typedef struct SpeexResamplerState_ SpeexResamplerState;

enum {
    RESAMPLER_ERR_SUCCESS      = 0,
    RESAMPLER_ERR_ALLOC_FAILED = 1,
    RESAMPLER_ERR_BAD_STATE    = 2,
    RESAMPLER_ERR_INVALID_ARG  = 3,
    RESAMPLER_ERR_PTR_OVERLAP  = 4,
    RESAMPLER_ERR_MAX_ERROR
};

#define SPEEX_RESAMPLER_QUALITY_MIN     0
#define SPEEX_RESAMPLER_QUALITY_DEFAULT 4
#define SPEEX_RESAMPLER_QUALITY_VOIP    3
#define SPEEX_RESAMPLER_QUALITY_DESKTOP 5
#define SPEEX_RESAMPLER_QUALITY_MAX     10
#define SPEEX_RESAMPLER_QUALITY_0       0
#define SPEEX_RESAMPLER_QUALITY_1       1
#define SPEEX_RESAMPLER_QUALITY_2       2
#define SPEEX_RESAMPLER_QUALITY_3       3
#define SPEEX_RESAMPLER_QUALITY_4       4
#define SPEEX_RESAMPLER_QUALITY_5       5
#define SPEEX_RESAMPLER_QUALITY_6       6
#define SPEEX_RESAMPLER_QUALITY_7       7
#define SPEEX_RESAMPLER_QUALITY_8       8
#define SPEEX_RESAMPLER_QUALITY_9       9
#define SPEEX_RESAMPLER_QUALITY_10      10

#ifdef __cplusplus
extern "C" {
#endif

SpeexResamplerState *speex_resampler_init(
    spx_uint32_t nb_channels,
    spx_uint32_t in_rate,
    spx_uint32_t out_rate,
    int          quality,
    int         *err);

void speex_resampler_destroy(SpeexResamplerState *st);

int speex_resampler_process_interleaved_int(
    SpeexResamplerState   *st,
    const spx_int16_t     *in,
    spx_uint32_t          *in_len,
    spx_int16_t           *out,
    spx_uint32_t          *out_len);

const char *speex_resampler_strerror(int err);

#ifdef __cplusplus
}
#endif
