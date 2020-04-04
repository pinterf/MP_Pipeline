#pragma once

#include "avisynth.h"
#include "Handle.h"
#include "TwoSidedLock.h"
#include "NonCopyableClassBase.h"
#include "SystemChar.h"
#include "SpinLock.h"
#include "CondVar.h"
#include "slave_common.h"

#include <string>
#include <vector>
#include <memory>

static const unsigned int SHARED_MEMORY_SOURCE_SIGNATURE = 0x4d50534d;

typedef enum _request_type_t
{
    REQ_EMPTY = 0,
    REQ_GETFRAME,
    REQ_GETPARITY
} request_type_t;

typedef struct _shared_memory_source_request_t
{
    request_type_t request_type;
    int clip_index;
    int frame_number;
    int sequence_number;
} shared_memory_source_request_t;


typedef union _parity_response_t
{
    struct
    {
        int frame_number;
        short sequence_number;
        signed char requested_client_count;
        char parity;
    };
    __int64 value;
} parity_response_t;

typedef struct _frame_response_t
{

    spin_lock_value_type_t lock;

    int frame_number;
    long requested_client_count;

    bool is_prefetch;
    int prefetch_hit;

} frame_response_t;

typedef __declspec(align(64)) struct _shared_memory_clip_info_t
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
    // --------------------------------------

    // align the volatile part to beginning of a cache line, 
    // so only one cache line is affected when someone changes its content
    // (don't know whether it is necessary, but memory is cheap :P )
    __declspec(align(64))
    volatile struct
    {
        // response index:
        // LSB of the frame number is used as index to the array

        parity_response_t parity_response[2];

        frame_response_t frame_response[2];
    };
} shared_memory_clip_info_t;

typedef struct _shared_memory_source_header_t
{
    unsigned int signature;
    int clip_count;
    DWORD server_process_id; // for debugging

    DWORD client_process_ids[MAX_SLAVES + 1];

    volatile struct
    {
        // pack states to a single int, so that we can atomically change it
        union
        {
            struct
            {
                unsigned short client_count;
                unsigned char shutdown;
                unsigned char sequence_number;
            };
            long state_value;
        } object_state;


        spin_lock_value_type_t request_lock;
        shared_memory_source_request_t request;
    };

    char error_msg[1024];
    shared_memory_clip_info_t clips[1];
} shared_memory_source_header_t;

#define member_size(type, member) sizeof(((type *)0)->member)

static_assert(member_size(shared_memory_source_header_t, object_state) == sizeof(long), "Size of object_state must be the same as size of long.");
static_assert(member_size(shared_memory_clip_info_t, parity_response[0]) == sizeof(__int64), "Size of parity_response must be the same as size of __int64.");

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

    void check_data_buffer_integrity(int clip_index, int response_object_id);

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