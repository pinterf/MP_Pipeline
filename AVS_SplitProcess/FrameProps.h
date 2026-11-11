#pragma once

// Frame property (AVSMap) serialization for passing frames between processes.
//
// The stream is a flat, pointer-free, little-endian byte block, identical for
// 32 and 64 bit processes. Supported property types: int, float, data (with its
// data type hint). Clip and frame properties can not cross a process boundary,
// they are skipped and only counted.
//
// Layout (all offsets relative to the start of the block, which must be 8 byte aligned):
//
//   header (24 bytes)
//     u32 magic          'MPFP'
//     u16 version        FRAMEPROPS_FORMAT_VERSION
//     u16 header_size    24
//     u32 total_size     size of the whole block including the header
//     u32 key_count      number of entries following the header
//     u32 skipped_count  number of keys not serialized (clip/frame types)
//     u32 reserved       0
//   entry, key_count times
//     u16 key_len, u8 type ('i', 'f', 's'), u8 reserved, u32 element_count
//     key bytes (no terminating zero), padded to 8 bytes
//     'i': element_count x int64     'f': element_count x double
//     's': element_count x { u32 length, i32 type_hint, bytes padded to 8 bytes }
//
// Numeric arrays are 8 byte aligned, so the reader passes them to
// propSetIntArray/propSetFloatArray directly from the block without a copy.

#include "avisynth.h"
#include <stddef.h>
#include <stdint.h>

static const uint32_t FRAMEPROPS_MAGIC = 0x5046504D; // "MPFP"
static const uint16_t FRAMEPROPS_FORMAT_VERSION = 1;
static const size_t FRAMEPROPS_HEADER_SIZE = 24;

// Frame property capabilities of an IScriptEnvironment, detect once per environment.
struct FramePropsCaps
{
    bool has_props;      // interface V8+: frame properties exist at all
    bool has_type_hint;  // interface V11+: propGetDataTypeHint / propSetDataH
};

FramePropsCaps detect_frame_props_caps(IScriptEnvironment* env);

// Serializes the properties of 'frame'.
// dst == nullptr: measure only, returns the required block size.
// dst != nullptr: writes the block, dst_capacity must be at least the measured size
//                 (std::runtime_error otherwise).
// Returns 0 when there is nothing to pass (no property support or empty map); in this case nothing is written.
size_t serialize_frame_props(IScriptEnvironment* env, const FramePropsCaps& caps, const PVideoFrame& frame, uint8_t* dst, size_t dst_capacity);

// Adds the properties stored in 'src' to the (writable) 'frame'.
// src_size == 0 means no properties. Throws std::runtime_error on a malformed block.
void deserialize_frame_props(IScriptEnvironment* env, const FramePropsCaps& caps, PVideoFrame& frame, const uint8_t* src, size_t src_size);
