#pragma once

#include <assert.h>
#include <stdint.h>
#include "avisynth.h"

int sprintf_append(char* buffer, const char* format, ...);

// microseconds of a monotonic clock, for measuring durations
int64_t us_time();

// Case-insensitive equality for ASCII names and keywords (statement values, platform names), like
// streqi of AviSynth+: no locale dependency, other bytes (e.g. UTF-8) must match exactly.
inline bool streqi(const char* s1, const char* s2)
{
    auto to_lower = [](char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; };
    while (*s1 || *s2)
    {
        if (to_lower(*s1) != to_lower(*s2))
        {
            return false;
        }
        ++s1;
        ++s2;
    }
    return true;
}




#define CACHE_LINE_SIZE 64

template <typename T>
static inline T aligned(T size, int alignment = CACHE_LINE_SIZE)
{
    // ensure alignment is power of 2
    assert(alignment > 0 && (alignment & (alignment - 1)) == 0);

    return (T)(((intptr_t)size + (alignment - 1)) & ~(intptr_t)(alignment - 1));
}

void copy_plane(unsigned char* dst, int dst_pitch, PVideoFrame& frame, const VideoInfo& vi, int plane);
void copy_plane(PVideoFrame& frame, const unsigned char* src, int src_pitch, const VideoInfo& vi, int plane);
