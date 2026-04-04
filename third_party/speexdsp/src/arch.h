#pragma once
#include <stdint.h>
typedef int16_t  spx_int16_t;
typedef uint16_t spx_uint16_t;
typedef int32_t  spx_int32_t;
typedef uint32_t spx_uint32_t;
typedef int64_t  spx_int64_t;
typedef uint64_t spx_uint64_t;
typedef spx_int16_t spx_word16_t;
typedef spx_int32_t spx_word32_t;
#ifndef WORD2INT
#define WORD2INT(x) ((x) < -32767 ? (spx_int16_t)-32768 : ((x) > 32767 ? (spx_int16_t)32767 : (spx_int16_t)(x)))
#endif
#define MULT16_16(a,b)     ((spx_word32_t)(spx_word16_t)(a)*(spx_word32_t)(spx_word16_t)(b))
#define MULT16_32_Q15(a,b) (((spx_word32_t)(spx_word16_t)(a)*(b))>>15)
#define MULT16_32_Q16(a,b) (((spx_word32_t)(spx_word16_t)(a)*(b))>>16)
#define MAC16_16(c,a,b)    ((c)+MULT16_16(a,b))
#define EXTEND32(x)        ((spx_word32_t)(x))
#define SHR32(a,shift)     ((a)>>(shift))
#define SHL32(a,shift)     ((a)<<(shift))
