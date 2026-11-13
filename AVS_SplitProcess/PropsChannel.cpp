#include "common.h"
#include "PropsChannel.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sstream>
#include <stdexcept>

namespace {

// Overflow mapping events (allocate, resize, release, client remap) are rare, so they can be
// logged in release builds too: set MPP_TRACE_PROPS=1 (inherited by the slave processes) and
// watch with DebugView or tests/tools/dbwin_listen.
bool props_log_enabled()
{
    static const bool enabled = [] {
        char value[8] = { 0 };
        return GetEnvironmentVariableA("MPP_TRACE_PROPS", value, sizeof(value)) > 0 && value[0] != '0';
    }();
    return enabled;
}

void props_log(const char* format, ...)
{
    if (!props_log_enabled())
    {
        return;
    }
    char buffer[512];
    int len = _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "MPP_Props[%lu]: ", GetCurrentProcessId());
    va_list args;
    va_start(args, format);
    _vsnprintf_s(buffer + len, sizeof(buffer) - len, _TRUNCATE, format, args);
    va_end(args);
    OutputDebugStringA(buffer);
}

sys_string overflow_mapping_name(const sys_string& key, int clip_index, int slot, uint32_t gen)
{
    std::wostringstream ss;
    ss << key << L"_Props_" << clip_index << L"_" << slot << L"_" << gen;
    return ss.str();
}

// overflow capacity for a payload: 1.5x headroom, rounded up to the granularity
size_t overflow_capacity_for(size_t size)
{
    size_t capacity = size + size / 2;
    capacity = (capacity + PROPS_OVERFLOW_GRANULARITY - 1) / PROPS_OVERFLOW_GRANULARITY * PROPS_OVERFLOW_GRANULARITY;
    if (capacity > PROPS_MAX_SIZE)
    {
        capacity = PROPS_MAX_SIZE;
    }
    return capacity;
}

} // namespace

// ---------------------------------------------------------------------------
// PropsWriter

PropsWriter::PropsWriter(const sys_string& key, int clip_index, int slot) :
    _key(key),
    _clip_index(clip_index),
    _slot(slot),
    _mapping(NULL),
    _view(nullptr),
    _capacity(0),
    _gen(0),
    _window_peak(0),
    _window_count(0),
    _realloc_count(0)
{
}

PropsWriter::~PropsWriter()
{
    if (_view)
    {
        UnmapViewOfFile(_view);
    }
}

void PropsWriter::reallocate(size_t capacity, volatile props_slot_state_t& state)
{
    props_log("server clip %d slot %d: overflow mapping %u -> %u bytes (gen %u, window peak %u)",
        _clip_index, _slot, (unsigned)_capacity, (unsigned)capacity, _gen + 1, (unsigned)_window_peak);

    // drop the old mapping; a client still having it open keeps it alive until it switches
    if (_view)
    {
        UnmapViewOfFile(_view);
        _view = nullptr;
    }
    _mapping.replace(NULL);
    _capacity = 0;
    _gen++;
    _realloc_count++;
    _window_peak = 0;
    _window_count = 0;

    // publish "no overflow mapping" first, so a failure below leaves a consistent state
    state.overflow_capacity = 0;
    state.overflow_gen = _gen;

    if (capacity == 0)
    {
        return;
    }

    const sys_string name = overflow_mapping_name(_key, _clip_index, _slot, _gen);
    const unsigned __int64 size64 = capacity;
    _mapping.replace(CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        (DWORD)(size64 >> 32), (DWORD)(size64 & 0xFFFFFFFF), name.c_str()));
    if (!_mapping.is_valid())
    {
        throw std::runtime_error("Unable to create frame property mapping (out of memory?).");
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        _mapping.replace(NULL);
        throw std::runtime_error("Frame property mapping already exists.");
    }
    _view = (uint8_t*)MapViewOfFile(_mapping.get(), FILE_MAP_WRITE, 0, 0, 0);
    if (!_view)
    {
        _mapping.replace(NULL);
        throw std::runtime_error("Unable to map frame property mapping (out of address space?).");
    }
    _capacity = capacity;
    state.overflow_capacity = (uint32_t)capacity;
}

void PropsWriter::write(IScriptEnvironment* env, const FramePropsCaps& caps, const PVideoFrame& frame,
    volatile props_slot_state_t& state, uint8_t* inline_area)
{
    // until the new content is complete, the slot has no properties
    state.size = 0;
    state.in_overflow = 0;

    const size_t need = serialize_frame_props(env, caps, frame, nullptr, 0);
    if (need > PROPS_MAX_SIZE)
    {
        throw std::runtime_error("Frame properties of a single frame exceed 256 MB.");
    }
    const size_t overflow_need = need > PROPS_INLINE_SIZE ? need : 0;

    // shrink with hysteresis: decide at the end of each window, the peak includes this frame
    if (_capacity > 0)
    {
        if (overflow_need > _window_peak)
        {
            _window_peak = overflow_need;
        }
        if (++_window_count >= PROPS_SHRINK_WINDOW)
        {
            if (_window_peak == 0)
            {
                reallocate(0, state);
            }
            else if (_window_peak < _capacity / 4)
            {
                reallocate(overflow_capacity_for(_window_peak), state);
            }
            else
            {
                _window_peak = 0;
                _window_count = 0;
            }
        }
    }

    if (need == 0)
    {
        return;
    }
    if (overflow_need == 0)
    {
        serialize_frame_props(env, caps, frame, inline_area, PROPS_INLINE_SIZE);
    }
    else
    {
        // grow immediately
        if (need > _capacity)
        {
            reallocate(overflow_capacity_for(need), state);
        }
        serialize_frame_props(env, caps, frame, _view, _capacity);
        state.in_overflow = 1;
    }
    state.size = (uint32_t)need;
}

// ---------------------------------------------------------------------------
// PropsReader

PropsReader::PropsReader(const sys_string& key, int clip_index, int slot) :
    _key(key),
    _clip_index(clip_index),
    _slot(slot),
    _mapping(NULL),
    _view(nullptr),
    _view_size(0),
    _gen(0)
{
}

PropsReader::~PropsReader()
{
    release_view();
}

void PropsReader::release_view()
{
    if (_view)
    {
        UnmapViewOfFile(_view);
        _view = nullptr;
    }
    _view_size = 0;
    _mapping.replace(NULL);
}

void PropsReader::read(IScriptEnvironment* env, const FramePropsCaps& caps, PVideoFrame& frame,
    const volatile props_slot_state_t& state, const uint8_t* inline_area)
{
    props_slot_state_t s;
    s.size = state.size;
    s.in_overflow = state.in_overflow;
    s.overflow_gen = state.overflow_gen;
    s.overflow_capacity = state.overflow_capacity;

    // the server reallocated or released its overflow mapping: let ours go
    if (s.overflow_gen != _gen)
    {
        if (_view)
        {
            props_log("client clip %d slot %d: unmapped overflow gen %u", _clip_index, _slot, _gen);
        }
        release_view();
        _gen = s.overflow_gen;
    }

    if (!caps.has_props || s.size == 0)
    {
        return;
    }

    const uint8_t* src;
    if (!s.in_overflow)
    {
        if (s.size > PROPS_INLINE_SIZE)
        {
            throw std::runtime_error("Invalid frame property state (inline size).");
        }
        src = inline_area;
    }
    else
    {
        if (s.size > s.overflow_capacity)
        {
            throw std::runtime_error("Invalid frame property state (overflow size).");
        }
        if (!_view)
        {
            const sys_string name = overflow_mapping_name(_key, _clip_index, _slot, _gen);
            _mapping.replace(OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str()));
            if (!_mapping.is_valid())
            {
                throw std::runtime_error("Unable to open frame property mapping.");
            }
            _view = (const uint8_t*)MapViewOfFile(_mapping.get(), FILE_MAP_READ, 0, 0, 0);
            if (!_view)
            {
                _mapping.replace(NULL);
                throw std::runtime_error("Unable to map frame property mapping (out of address space?).");
            }
            _view_size = s.overflow_capacity;
            props_log("client clip %d slot %d: mapped overflow gen %u, %u bytes", _clip_index, _slot, _gen, (unsigned)_view_size);
        }
        if (s.size > _view_size)
        {
            throw std::runtime_error("Invalid frame property state (view size).");
        }
        src = _view;
    }
    deserialize_frame_props(env, caps, frame, src, s.size);
}
