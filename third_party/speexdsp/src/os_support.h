#pragma once
#include <stdlib.h>
#include <string.h>
static __inline void *speex_alloc(int size) { return calloc((size_t)(size), 1); }
static __inline void *speex_realloc(void *ptr, int size) { return realloc(ptr, (size_t)(size)); }
static __inline void  speex_free(void *ptr) { free(ptr); }
#ifndef SPEEX_INLINE
#define SPEEX_INLINE __inline
#endif
