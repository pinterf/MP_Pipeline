#pragma once

// Frame property transport between the server and client processes, one channel per
// clip and response slot.
//
// Two tiers:
//  - Tier 0: a small inline area in the main shared memory mapping, always present.
//            Holds the usual properties (_Matrix, _FieldBased, scene stats...) with no
//            extra handle or remapping.
//  - Tier 1: an overflow mapping, created on demand when a frame's properties don't fit
//            inline. Grows immediately, shrinks with hysteresis (peak over a window of
//            frames below 1/4 of the capacity), and is released completely after a
//            window without overflow. Each (re)allocation gets a new generation number,
//            which is part of the mapping name; the client reopens the mapping when it
//            sees a new generation. An old mapping stays alive (kernel reference count)
//            until the last process closes its view, so dropping it on the server side
//            is always safe.
//
// All calls must be made while holding the response slot's lock, the same lock that
// protects the frame buffer of the slot.

#include "avisynth.h"
#include "FrameProps.h"
#include "Handle.h"
#include "NonCopyableClassBase.h"
#include "SystemChar.h"
#include <stdint.h>
#include <string>

// size of the inline (tier 0) area of each response slot
static const uint32_t PROPS_INLINE_SIZE = 4096;
// overflow mapping sizes are multiples of this
static const size_t PROPS_OVERFLOW_GRANULARITY = 64 * 1024;
// safety limit for the properties of a single frame
static const size_t PROPS_MAX_SIZE = 256 * 1024 * 1024;
// number of frames written to a slot before the overflow capacity is reconsidered
static const int PROPS_SHRINK_WINDOW = 64;

// Per slot state in shared memory. Same layout for 32 and 64 bit processes.
typedef struct _props_slot_state_t
{
    uint32_t size;              // size of the serialized properties, 0: none
    uint32_t in_overflow;       // 0: data is in the inline area, 1: in the overflow mapping
    uint32_t overflow_gen;      // generation of the current overflow mapping
    uint32_t overflow_capacity; // size of the current overflow mapping, 0: none
} props_slot_state_t;

// Server side of one slot.
class PropsWriter : private NonCopyableClassBase
{
public:
    PropsWriter(const sys_string& key, int clip_index, int slot);
    ~PropsWriter();

    // Replaces the slot's properties with those of 'frame'. Throws std::runtime_error.
    void write(IScriptEnvironment* env, const FramePropsCaps& caps, const PVideoFrame& frame,
        volatile props_slot_state_t& state, uint8_t* inline_area);

    // statistics, for tracing and tests
    unsigned int realloc_count() const { return _realloc_count; }

private:
    void reallocate(size_t capacity, volatile props_slot_state_t& state);

    sys_string _key;
    int _clip_index;
    int _slot;

    OwnedHandle _mapping;
    uint8_t* _view;
    size_t _capacity;
    uint32_t _gen;

    size_t _window_peak;
    int _window_count;
    unsigned int _realloc_count;
};

// Client side of one slot.
class PropsReader : private NonCopyableClassBase
{
public:
    PropsReader(const sys_string& key, int clip_index, int slot);
    ~PropsReader();

    // Adds the slot's properties to 'frame'. Throws std::runtime_error.
    void read(IScriptEnvironment* env, const FramePropsCaps& caps, PVideoFrame& frame,
        const volatile props_slot_state_t& state, const uint8_t* inline_area);

private:
    void release_view();

    sys_string _key;
    int _clip_index;
    int _slot;

    OwnedHandle _mapping;
    const uint8_t* _view;
    size_t _view_size;
    uint32_t _gen;
};
