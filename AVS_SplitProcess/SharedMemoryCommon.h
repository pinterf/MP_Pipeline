#pragma once

#include "avisynth.h"
#include "Handle.h"
#include "TwoSidedLock.h"
#include "NonCopyableClassBase.h"
#include "SystemChar.h"
#include "SpinLock.h"
#include "CondVar.h"
#include "slave_common.h"
#include "PropsChannel.h"

#include <stddef.h>
#include <stdint.h>
#include <atomic>
#include <string>
#include <vector>
#include <memory>

// change it whenever the layout below changes, so that mismatching server and client
// binaries (e.g. a stale .win32/.win64 slave) fail cleanly instead of misreading the mapping
static const unsigned int SHARED_MEMORY_SOURCE_SIGNATURE = 0x4d50534e;

// Planes of a frame in the order they are stored in the shared frame buffer, returns their count:
// packed formats (RGB24/32/48/64, YUY2) and Y: 1 (PLANAR_Y means the whole packed frame),
// YA: Y, A; YUV(A): Y, U, V(, A); planar RGB(A): G, B, R(, A).
// Uses only VideoInfo functions older than interface V13, also for the V13 formats.
int get_transfer_planes(const VideoInfo& vi, int planes[4]);

// pixel_type is a format introduced by AviSynth+ interface V13 (YA, 4:4:0, alpha or more than
// 8 bits for 4:1:1 and 4:1:0); an older core in the process can't create its frames
bool is_v13_pixel_type(int pixel_type);

typedef enum _request_type_t
{
    REQ_EMPTY = 0,
    REQ_GETFRAME,
    REQ_GETPARITY
} request_type_t;

// a request, local copy
typedef struct _shared_memory_source_request_t
{
    request_type_t request_type;
    int clip_index;
    int frame_number;
    int sequence_number;
} shared_memory_source_request_t;

// The request slot in the shared memory, protected by request_lock.
// sequence_number is also read without the lock (by the client checking whether the server
// took its request), so every field is atomic.
typedef struct _shared_request_slot_t
{
    std::atomic<int> request_type; // request_type_t
    std::atomic<int> clip_index;
    std::atomic<int> frame_number;
    std::atomic<int> sequence_number;
} shared_request_slot_t;

// a parity response, local copy, packed into the 64 bit value of the shared slot
typedef union _parity_response_t
{
    struct
    {
        int frame_number;
        short sequence_number;
        signed char requested_client_count;
        char parity;
    };
    int64_t value;
} parity_response_t;

typedef struct _frame_response_t
{
    // protects the frame buffer, props, and the changes of the fields below
    spin_lock_t lock;

    // atomic: the server reads them without taking the lock (only its thread changes
    // frame_number), and the client count changes on both sides
    std::atomic<int> frame_number;
    std::atomic<long> requested_client_count;

    std::atomic<bool> is_prefetch;
    std::atomic<int> prefetch_hit;

    // frame properties of the frame in this response slot, protected by the lock
    props_slot_state_t props;

} frame_response_t;

// alignas instead of __declspec(align): GCC ignores the latter here, which would give a
// different layout than the MSVC builds (the other side of the mapping may be either)
typedef struct alignas(64) _shared_memory_clip_info_t
{
    // --- immutable after initialization ---
    VideoInfo vi;

    // buffer size of ONE frame
    unsigned int frame_buffer_size;
    // relative to the beginning of the mapped region
    unsigned int frame_buffer_offset[2];

    int frame_pitch;
    int frame_pitch_uv;
    int frame_pitch_a;

    // relative to the beginning of buffer 
    int frame_offset_u;
    int frame_offset_v;
    int frame_offset_a;

    // relative to the beginning of the mapped region, PROPS_INLINE_SIZE bytes each
    unsigned int props_inline_offset[2];
    // --------------------------------------

    // the changing part starts on a cache line, so only one cache line is affected when
    // someone changes its content

    // response index: LSB of the frame number is used as index to the arrays
    // parity_response_t values, changed with compare_exchange
    alignas(64) std::atomic<int64_t> parity_response[2];

    frame_response_t frame_response[2];
} shared_memory_clip_info_t;

// position of a plane (one of get_transfer_planes) in a frame buffer of the clip
inline int plane_buffer_offset(const shared_memory_clip_info_t& info, int plane)
{
    switch (plane)
    {
    case PLANAR_U: case PLANAR_B: return info.frame_offset_u;
    case PLANAR_V: case PLANAR_R: return info.frame_offset_v;
    case PLANAR_A: return info.frame_offset_a;
    default: return 0; // Y, G, packed
    }
}

inline int plane_buffer_pitch(const shared_memory_clip_info_t& info, int plane)
{
    switch (plane)
    {
    case PLANAR_U: case PLANAR_B: case PLANAR_V: case PLANAR_R: return info.frame_pitch_uv;
    case PLANAR_A: return info.frame_pitch_a;
    default: return info.frame_pitch;
    }
}

// the object state, local copy, packed into the long value of the shared object_state
typedef union _object_state_t
{
    struct
    {
        unsigned short client_count;
        unsigned char shutdown;
        unsigned char sequence_number;
    };
    long state_value;
} object_state_t;

typedef struct _shared_memory_source_header_t
{
    unsigned int signature;
    int clip_count;
    DWORD server_process_id; // for debugging

    std::atomic<DWORD> client_process_ids[MAX_SLAVES + 1];

    // object_state_t value, changed with compare_exchange
    std::atomic<long> object_state;

    spin_lock_t request_lock;
    shared_request_slot_t request;

    // written by the server before it signals the shutdown in object_state
    char error_msg[1024];
    shared_memory_clip_info_t clips[1];
} shared_memory_source_header_t;

inline object_state_t load_object_state(const shared_memory_source_header_t* header)
{
    object_state_t state;
    state.state_value = header->object_state.load();
    return state;
}

// The layout must be the same for every compiler and bitness (both sides of the mapping).
// The atomics must be lock-free (address-free, so they work across processes) and have the
// size and alignment of the plain types.
static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_LONG_LOCK_FREE == 2 && ATOMIC_LLONG_LOCK_FREE == 2 && ATOMIC_BOOL_LOCK_FREE == 2,
    "The shared memory atomics must be lock-free.");
static_assert(sizeof(std::atomic<int>) == 4 && sizeof(std::atomic<long>) == 4 && sizeof(std::atomic<DWORD>) == 4 && sizeof(std::atomic<bool>) == 1,
    "Unexpected std::atomic size.");
static_assert(sizeof(std::atomic<int64_t>) == 8 && alignof(std::atomic<int64_t>) == 8, "std::atomic<int64_t> must be 8 bytes, 8 byte aligned.");
static_assert(sizeof(object_state_t) == sizeof(long), "Size of object_state_t must be the same as size of long.");
static_assert(sizeof(parity_response_t) == sizeof(int64_t), "Size of parity_response_t must be the same as size of int64_t.");
static_assert(alignof(shared_memory_clip_info_t) == 64, "shared_memory_clip_info_t must be 64 byte aligned.");
static_assert(offsetof(shared_memory_clip_info_t, parity_response) % 64 == 0, "The changing part of shared_memory_clip_info_t must start on a cache line.");

class ClipSyncGroup
{
public:
    ClipSyncGroup(const sys_string& name, int clip_index, shared_memory_clip_info_t& clip_info, bool is_server);
    std::vector<std::unique_ptr<CondVar> > response_conds;
};

// provide compatibility with TCPDeliver
sys_string get_shared_memory_key(const char* key1, int key2);

int get_response_index(int frame_number);

class SharedMemorySourceManager : private NonCopyableClassBase
{
public:
    
    SharedMemorySourceManager(const sys_string key, int clip_count, const VideoInfo vi_array[]);
    SharedMemorySourceManager(const sys_string key);
    ~SharedMemorySourceManager();
    shared_memory_source_header_t* header;
    // base name of all kernel objects of this instance
    const sys_string key;

    void check_data_buffer_integrity(int clip_index, int response_object_id);

    unsigned char* frame_buffer(int clip_index, int response_index) const
    {
        return (unsigned char*)header + header->clips[clip_index].frame_buffer_offset[response_index];
    }
    unsigned char* props_inline_area(int clip_index, int response_index) const
    {
        return (unsigned char*)header + header->clips[clip_index].props_inline_offset[response_index];
    }

    void signal_shutdown();

    std::unique_ptr<CondVar> request_cond;
    std::unique_ptr<TwoSidedLock> parity_signal;
    std::vector< std::unique_ptr<ClipSyncGroup> > sync_groups;

private:
    void init_server(const SYSCHAR* mapping_name, int clip_count, const VideoInfo vi_array[]);
    void init_client(const SYSCHAR* mapping_name);
    void init_sync_objects(const sys_string& key, int clip_count);
    void map_view();

    bool _is_server;

    OwnedHandle _mapping_handle;
    
};

class SharedMemorySourceClientLockAcquire : private NonCopyableClassBase
{
public:
    SharedMemorySourceClientLockAcquire(SharedMemorySourceManager& manager);
    ~SharedMemorySourceClientLockAcquire();
private:
    void do_lock(short increment);
    SharedMemorySourceManager& _manager;
};