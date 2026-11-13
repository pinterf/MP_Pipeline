#pragma once

#include <assert.h>
#include <stdint.h>
#include "avisynth.h"

int sprintf_append(char* buffer, const char* format, ...);

// microseconds of a monotonic clock, for measuring durations
int64_t us_time();


#ifdef _M_X64
typedef __int64 NATIVE_INT;
#else
typedef int NATIVE_INT;
#endif

#define CACHE_LINE_SIZE 64

template <typename T>
static T __inline aligned(T size, int alignment = CACHE_LINE_SIZE)
{
    // ensure alignment is power of 2
    assert(alignment > 0 && (alignment & (alignment - 1)) == 0);

    return (T)(((NATIVE_INT)size + (alignment - 1)) & ~(alignment - 1));
}

void copy_plane(unsigned char* dst, int dst_pitch, PVideoFrame& frame, const VideoInfo& vi, int plane);
void copy_plane(PVideoFrame& frame, const unsigned char* src, int src_pitch, const VideoInfo& vi, int plane);
