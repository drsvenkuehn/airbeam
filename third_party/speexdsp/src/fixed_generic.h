#pragma once
#include "arch.h"
#define MULT16_16_P15(a,b) ((spx_word32_t)((MULT16_16(a,b)+16384)>>15))
#define SATURATE16(x,a)    (((x)>(a))?(a):(((x)<-(a))?(-(a)):(x)))
#define SATURATE32(x,a)    (((x)>(a))?(a):(((x)<-(a))?(-(a)):(x)))
