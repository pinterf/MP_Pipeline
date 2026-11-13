#define _CRT_NON_CONFORMING_SWPRINTFS

#include "common.h"
#include "utils.h"
#include <algorithm>

int sprintf_append(char* buffer, const char* format, ...)
{
    size_t len = strlen(buffer);
    va_list args;
    va_start(args, format);
    return vsprintf(buffer + len, format, args);
}

int wsprintf_append(wchar_t* buffer, const wchar_t* format, ...)
{
    size_t len = wcslen(buffer);
    va_list args;
    va_start(args, format);
    return _vswprintf(buffer + len, format, args);
}


bool get_self_path_a(char* buffer, DWORD buffer_size)
{
    HMODULE module = NULL;
    if (!GetModuleHandleEx(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | 
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPTSTR)get_self_path,
        &module))
    {
        return false;
    }
    buffer[0] = 0;
    buffer[buffer_size - 1] = 0;
    return !!GetModuleFileNameA(module, buffer, buffer_size - 1);
}

bool get_self_path_w(wchar_t* buffer, DWORD buffer_size)
{
    HMODULE module = NULL;
    if (!GetModuleHandleEx(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | 
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPTSTR)get_self_path,
        &module))
    {
        return false;
    }
    buffer[0] = 0;
    buffer[buffer_size - 1] = 0;
    return !!GetModuleFileNameW(module, buffer, buffer_size - 1);
}

__int64 us_time()
{
    LARGE_INTEGER ticks, ticksPerSecond;
    QueryPerformanceFrequency(&ticksPerSecond);
    QueryPerformanceCounter(&ticks);

    return ticks.QuadPart * 1000 * 1000 / ticksPerSecond.QuadPart;
}

void copy_plane(unsigned char* dst, int dst_pitch, const unsigned char* src, int src_pitch, const VideoInfo& vi, int plane)
{
    assert(dst);
    assert(src);

    int row_size = vi.RowSize(plane);
    int height = vi.height >> vi.GetPlaneHeightSubsampling(plane);

    assert(dst_pitch >= src_pitch);
    assert(dst_pitch >= row_size && src_pitch >= row_size);

    // full aligned copy when possible, dst is always aligned so don't need to check it
    int line_size = (std::min)(aligned(row_size, FRAME_ALIGN), src_pitch);

    if (line_size == src_pitch && src_pitch == dst_pitch)
    {
        memcpy(dst, src, line_size * height);
    } else {
        for (int i = 0; i < height; i++)
        {
            memcpy(dst, src, line_size);
            dst += dst_pitch;
            src += src_pitch;
        }
    }
}

void copy_plane(unsigned char* dst, int dst_pitch, PVideoFrame& frame, const VideoInfo& vi, int plane)
{
    copy_plane(dst, dst_pitch, frame->GetReadPtr(plane), frame->GetPitch(plane), vi, plane);
}

void copy_plane(PVideoFrame& frame, const unsigned char* src, int src_pitch, const VideoInfo& vi, int plane)
{
    copy_plane(frame->GetWritePtr(plane), frame->GetPitch(plane), src, src_pitch, vi, plane);
}
