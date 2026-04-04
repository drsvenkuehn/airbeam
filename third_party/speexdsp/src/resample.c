/* Speex resampler — rational polyphase linear-interpolation resampler (FIXED_POINT mode)
 * Copyright 2007 Jean-Marc Valin — BSD-3-Clause (vendored for AirBeam)
 * Simplified to linear-interpolation polyphase for FIXED_POINT path.
 */

#include "os_support.h"
#include "arch.h"
#include <speex_resampler.h>

/* GCD via Euclidean algorithm */
static spx_uint32_t gcd(spx_uint32_t a, spx_uint32_t b) {
    while (b) { spx_uint32_t t = b; b = a % b; a = t; }
    return a;
}

struct SpeexResamplerState_ {
    spx_uint32_t in_rate;
    spx_uint32_t out_rate;
    spx_uint32_t nb_channels;
    spx_uint32_t num_rate;     /* in_rate / GCD — input steps per output sample */
    spx_uint32_t den_rate;     /* out_rate / GCD — denominator for fraction */
    spx_uint32_t samp_frac_num; /* current phase, 0..den_rate-1 */
    int          quality;
    int          passthrough;
};

SpeexResamplerState *speex_resampler_init(spx_uint32_t nb_channels,
                                           spx_uint32_t in_rate,
                                           spx_uint32_t out_rate,
                                           int quality,
                                           int *err)
{
    if (nb_channels == 0 || in_rate == 0 || out_rate == 0) {
        if (err) *err = RESAMPLER_ERR_INVALID_ARG;
        return NULL;
    }
    SpeexResamplerState *st = (SpeexResamplerState *)speex_alloc((int)sizeof(*st));
    if (!st) { if (err) *err = RESAMPLER_ERR_ALLOC_FAILED; return NULL; }
    st->in_rate      = in_rate;
    st->out_rate     = out_rate;
    st->nb_channels  = nb_channels;
    st->quality      = quality;
    st->samp_frac_num = 0;
    st->passthrough  = (in_rate == out_rate) ? 1 : 0;
    {
        spx_uint32_t g   = gcd(in_rate, out_rate);
        st->num_rate     = in_rate  / g;   /* how much phase advances per output sample */
        st->den_rate     = out_rate / g;   /* denominator: fraction = samp_frac_num / den_rate */
    }
    if (err) *err = RESAMPLER_ERR_SUCCESS;
    return st;
}

void speex_resampler_destroy(SpeexResamplerState *st) {
    if (st) speex_free(st);
}

/* Process interleaved int16 frames.
 * in_len / out_len are in FRAMES (not samples).
 * On return: *in_len = frames consumed, *out_len = frames produced.
 */
int speex_resampler_process_interleaved_int(SpeexResamplerState *st,
                                             const spx_int16_t   *in,
                                             spx_uint32_t        *in_len,
                                             spx_int16_t         *out,
                                             spx_uint32_t        *out_len)
{
    if (!st) return RESAMPLER_ERR_BAD_STATE;

    spx_uint32_t max_in  = *in_len;
    spx_uint32_t max_out = *out_len;
    spx_uint32_t nb_ch   = st->nb_channels;
    spx_uint32_t num     = st->num_rate;   /* in_rate / GCD */
    spx_uint32_t den     = st->den_rate;   /* out_rate / GCD */
    spx_uint32_t frac    = st->samp_frac_num;

    spx_uint32_t in_idx  = 0;   /* current input frame index */
    spx_uint32_t out_idx = 0;   /* current output frame index */

    if (st->passthrough) {
        spx_uint32_t n = (max_in < max_out) ? max_in : max_out;
        spx_uint32_t i;
        for (i = 0; i < n * nb_ch; i++) out[i] = in[i];
        *in_len  = n;
        *out_len = n;
        return RESAMPLER_ERR_SUCCESS;
    }

    while (out_idx < max_out) {
        /* Need in[in_idx] and in[in_idx+1] for linear interpolation.
         * Exception: if frac==0, we only need in[in_idx] (exact sample). */
        if (frac == 0) {
            if (in_idx >= max_in) break;
        } else {
            if (in_idx + 1 >= max_in) break;
        }

        {
            spx_uint32_t ch;
            for (ch = 0; ch < nb_ch; ch++) {
                spx_int32_t s0 = in[in_idx       * nb_ch + ch];
                spx_int32_t s1 = (frac != 0 && in_idx + 1 < max_in)
                                 ? in[(in_idx + 1) * nb_ch + ch] : s0;
                /* Linear interpolation: out = s0 + (s1 - s0) * frac / den */
                spx_int32_t val = s0 + (spx_int32_t)((spx_int64_t)(s1 - s0) * (spx_int64_t)frac / (spx_int32_t)den);
                /* Clamp to int16 */
                if (val > 32767)  val =  32767;
                if (val < -32768) val = -32768;
                out[out_idx * nb_ch + ch] = (spx_int16_t)val;
            }
        }
        out_idx++;

        /* Advance phase by num_rate */
        frac += num;
        while (frac >= den) {
            frac -= den;
            in_idx++;
        }
    }

    st->samp_frac_num = frac;
    *in_len  = in_idx;
    *out_len = out_idx;
    return RESAMPLER_ERR_SUCCESS;
}

const char *speex_resampler_strerror(int err) {
    switch (err) {
    case RESAMPLER_ERR_SUCCESS:      return "Success";
    case RESAMPLER_ERR_ALLOC_FAILED: return "Memory allocation failed";
    case RESAMPLER_ERR_BAD_STATE:    return "Bad resampler state";
    case RESAMPLER_ERR_INVALID_ARG:  return "Invalid argument";
    case RESAMPLER_ERR_PTR_OVERLAP:  return "Input and output buffers overlap";
    default:                         return "Unknown error";
    }
}
