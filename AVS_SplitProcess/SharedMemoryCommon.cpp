#define UNICODE
#define _UNICODE

#include "stdafx.h"
#include "SharedMemoryCommon.h"

#include <stdio.h>
#include <stdexcept>
#include <assert.h>
#include <sstream>
#include "utils.h"

using namespace std;

ClipSyncGroup::ClipSyncGroup(const sys_string& name, int clip_index, shared_memory_clip_info_t& clip_info, bool is_server)
{
    for (int i = 0; i < 2; i++)
    {
        tostringstream ss;
        ss << name << SYSTEXT("_") << clip_index << SYSTEXT("_Response") << i;
        response_conds.push_back(unique_ptr<CondVar>(new CondVar(&clip_info.frame_response[i].lock, ss.str(), is_server)));
    }
}

sys_string get_shared_memory_key(const char* key1, int key2)
{
    if (strlen(key1) > 256)
    {
        throw invalid_argument("key1 is too long.");
    }
    SYSCHAR buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    _sntprintf(buffer, ARRAYSIZE(buffer) - 1, L"SharedMemorySource_%hs_%d", key1, key2);
    return sys_string(buffer);
}

int get_response_index(int frame_number)
{
    return frame_number & 1;
}


void add_guard_bytes(unsigned char* address, DWORD buffer_size)
{
    for (int i = 0; i < 4; i++)
    {
        *(((int*)address) - i - 1) = 0xDEADC0DE;
    }
    for (int i = 0; i < 4; i++)
    {
        *(((int*)(address + buffer_size)) + i) = 0xDEADBEEF;
    }
}

void check_guard_bytes(unsigned char* address, DWORD buffer_size)
{
#if _DEBUG
    for (int i = 0; i < 4; i++)
    {
        assert(*(((int*)address) - i - 1) == 0xDEADC0DE);
    }
    for (int i = 0; i < 4; i++)
    {
        assert(*(((int*)(address + buffer_size)) + i) == 0xDEADBEEF);
    }
#endif
}

void SharedMemorySourceManager::init_server(const SYSCHAR* mapping_name, int clip_count, const VideoInfo vi_array[])
{
    assert(clip_count > 0);

    // we will copy the structures to the right place later, 
    // so we don't need to align the allocation here
    #pragma warning(push)
    #pragma warning(disable: 4316) // Object allocated on the heap may not be aligned for this type.
    unique_ptr<shared_memory_clip_info_t[]> info_array(new shared_memory_clip_info_t[clip_count]);
    #pragma warning(pop)
    size_t clip_info_size = sizeof(shared_memory_clip_info_t) * clip_count;
    memset(&info_array[0], 0, clip_info_size);

    DWORD mapping_size = aligned((DWORD)(sizeof(shared_memory_source_header_t) + clip_info_size));
    for (int clip_i = 0; clip_i < clip_count; clip_i++)
    {
        shared_memory_clip_info_t& info = info_array[clip_i];
        info.vi = vi_array[clip_i];
        DWORD clip_buffer_size = aligned(info.vi.RowSize()) * info.vi.height;
        info.frame_pitch = aligned(info.vi.RowSize(), FRAME_ALIGN);

        const int planesYUV[4] = { PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A };
        const int planesRGB[4] = { PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A };
        const int* planes = info.vi.IsYUV() || info.vi.IsYUVA() ? planesYUV : planesRGB;

        if (info.vi.IsPlanar() && !info.vi.IsY())
        {
          DWORD y_size = clip_buffer_size;
          int uv_height = info.vi.height >> info.vi.GetPlaneHeightSubsampling(planes[1]);
          int uv_row_size = info.vi.RowSize(planes[1]);
          clip_buffer_size += aligned(uv_row_size) * uv_height * 2;

          int a_height = info.vi.NumComponents() == 4 ? info.vi.height : 0;
          int a_row_size = info.vi.RowSize(planes[3]);
          clip_buffer_size += aligned(a_row_size) * a_height;

          info.frame_pitch_uv = aligned(uv_row_size, FRAME_ALIGN);
          info.frame_offset_u = y_size;
          info.frame_offset_v = y_size + info.frame_pitch_uv * uv_height;

          info.frame_pitch_a = aligned(a_row_size, FRAME_ALIGN);
          info.frame_offset_a = info.frame_offset_v + info.frame_pitch_uv * uv_height;
        }
        info.frame_buffer_size = clip_buffer_size;

        // add some extra space before and after the buffer for guard bytes
        info.frame_response[0].frame_number = -1;
        info.frame_buffer_offset[0] = mapping_size + CACHE_LINE_SIZE * 2;
        mapping_size = aligned(mapping_size + clip_buffer_size + 2048);

        info.frame_response[1].frame_number = -1;
        info.frame_buffer_offset[1] = mapping_size + CACHE_LINE_SIZE * 2;
        mapping_size = aligned(mapping_size + clip_buffer_size + 2048);
    }

    _mapping_handle.replace(CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, mapping_size, mapping_name));
    if (!_mapping_handle.is_valid())
    {
        DWORD error_code = GetLastError();
        assert(false);
        throw runtime_error("Unable to create file mapping object.");
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        throw runtime_error("The file mapping object already exists.");
    }
    map_view();

    memset(header, 0, sizeof(shared_memory_source_header_t));
    header->signature = SHARED_MEMORY_SOURCE_SIGNATURE;
    header->clip_count = clip_count;
    header->server_process_id = GetCurrentProcessId();
    memcpy(header->clips, &info_array[0], clip_info_size);
    for (int i = 0; i < clip_count; i++)
    {
        add_guard_bytes((unsigned char*)header + header->clips[i].frame_buffer_offset[0], header->clips[i].frame_buffer_size);
        add_guard_bytes((unsigned char*)header + header->clips[i].frame_buffer_offset[1], header->clips[i].frame_buffer_size);
    }

}

void SharedMemorySourceManager::map_view()
{
    header = (shared_memory_source_header_t*)MapViewOfFile(_mapping_handle.get(), FILE_MAP_WRITE, 0, 0, 0);
    if (!header)
    {
        DWORD error_code = GetLastError();
        assert(false);
        throw runtime_error("MapViewOfFile failed.");
    }
}

void SharedMemorySourceManager::check_data_buffer_integrity(int clip_index, int response_object_id)
{
    assert(clip_index >= 0 && clip_index < header->clip_count);
    assert(response_object_id >= 0 && response_object_id <= 1);
    check_guard_bytes(
        (unsigned char*)header + header->clips[clip_index].frame_buffer_offset[response_object_id], 
        header->clips[clip_index].frame_buffer_size);
}

void SharedMemorySourceManager::signal_shutdown()
{
    if (_is_server)
    {
        while (true)
        {
            auto state = header->object_state;
            if (state.shutdown)
            {
                break;
            }
            long old_state_value = state.state_value;
            state.shutdown = 1;
            state.sequence_number++;
            if (_InterlockedCompareExchange(&header->object_state.state_value, state.state_value, old_state_value) == old_state_value)
            {
                break;
            }
        }
        request_cond->signal.signal_all();
        parity_signal->signal_all();
        for (size_t i = 0; i < sync_groups.size(); i++)
        {
            auto& conds = sync_groups[i]->response_conds;
            for (size_t j = 0; j < conds.size(); j++)
            {
                conds[j]->signal.signal_all();
            }
        }
    }
}

void SharedMemorySourceManager::init_client(const SYSCHAR* mapping_name)
{
    _mapping_handle.replace(OpenFileMapping(FILE_MAP_WRITE, FALSE, mapping_name));
    if (!_mapping_handle.is_valid())
    {
        DWORD error_code = GetLastError();
        assert(false);
        throw runtime_error("Unable to open the file mapping object, maybe the server is closed.");
    }
    map_view();
    if (header->signature != SHARED_MEMORY_SOURCE_SIGNATURE)
    {
        assert(false);
        throw runtime_error("Invalid shared memory object.");
    }
    DWORD process_id = GetCurrentProcessId();
    for (int i = 0; i < MAX_SLAVES; i++)
    {
        volatile DWORD& slot_reference = header->client_process_ids[i];
        if (InterlockedCompareExchange(&slot_reference, process_id, 0) == 0)
        {
            break;
        }
    }
}

void SharedMemorySourceManager::init_sync_objects(const sys_string& key, int clip_count)
{
    tstring cond_event_name(key);
    cond_event_name.append(SYSTEXT("_CondEvent"));
    request_cond = unique_ptr<CondVar>(new CondVar(&header->request_lock, cond_event_name, _is_server));

    parity_signal = unique_ptr<TwoSidedLock>(new TwoSidedLock(key.c_str(), SYSTEXT("ParitySignal"), _is_server));

    if (_is_server)
    {
        request_cond->signal.switch_to_other_side();
    }
    for (int i = 0; i < clip_count; i++)
    {
        sync_groups.push_back(unique_ptr<ClipSyncGroup>(new ClipSyncGroup(key, i, header->clips[i], _is_server)));
    }
}

sys_string get_mapping_name(const sys_string& key)
{
    sys_string mapping_name(key);
    mapping_name.append(SYSTEXT("_SharedMemoryObject"));
    return mapping_name;
}

SharedMemorySourceManager::SharedMemorySourceManager(const sys_string key, int clip_count, const VideoInfo vi_array[]) :
    _is_server(true),
    _mapping_handle(NULL),
    header(NULL)
{
    if (clip_count <= 0)
    {
        throw invalid_argument("clip_count must be greater than 0.");
    }


    init_server(get_mapping_name(key).c_str(), clip_count, vi_array);
    init_sync_objects(key, clip_count);
}
SharedMemorySourceManager::SharedMemorySourceManager(const sys_string key) :
    _is_server(false),
    _mapping_handle(NULL),
    header(NULL)
{
    init_client(get_mapping_name(key).c_str());
    init_sync_objects(key, header->clip_count);
}

SharedMemorySourceManager::~SharedMemorySourceManager()
{
    if (_is_server)
    {
        header->signature = 0xDEADC0DE;

        do
        {
            signal_shutdown();
            Sleep(1);
            bool has_alive_client = false;
            for (int i = 0; i < MAX_SLAVES; i++)
            {
                DWORD client_process_id = header->client_process_ids[i];
                if (!client_process_id)
                {
                    continue;
                }
                OwnedHandle process_handle(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, client_process_id));
                if (!process_handle.is_valid())
                {
                    continue;
                }
                DWORD exit_code = NULL;
                BOOL result = GetExitCodeProcess(process_handle.get(), &exit_code);
                assert(result);
                if (exit_code == STILL_ACTIVE)
                {
                    has_alive_client = true;
                    break;
                }
            }
            if (!has_alive_client)
            {
                // maybe some clients are terminated by task manager
                break;
            }
        } while (header->object_state.client_count > 0);
    } else {
        DWORD process_id = GetCurrentProcessId();
        for (int i = 0; i < MAX_SLAVES; i++)
        {
            if (header->client_process_ids[i] == process_id)
            {
                header->client_process_ids[i] = NULL;
                break;
            }
        }
    }
    UnmapViewOfFile(header);
    header = NULL;
}

void SharedMemorySourceClientLockAcquire::do_lock(short increment)
{
    while (true)
    {
        auto state = _manager.header->object_state;
        auto old_state_value = state.state_value;
        state.sequence_number++;
        state.client_count += increment;
        if (_InterlockedCompareExchange(&_manager.header->object_state.state_value, state.state_value, old_state_value) == old_state_value)
        {
            break;
        }
    }
}

SharedMemorySourceClientLockAcquire::SharedMemorySourceClientLockAcquire(SharedMemorySourceManager& manager) :
    _manager(manager)
{
    do_lock(1);
}

SharedMemorySourceClientLockAcquire::~SharedMemorySourceClientLockAcquire()
{
    do_lock(-1);
}