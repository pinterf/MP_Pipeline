#include "common.h"
#include "SharedMemoryCommon.h"

#include <stdio.h>
#include <stdexcept>
#include <assert.h>
#include <sstream>
#include <chrono>
#include <thread>
#include "utils.h"

using namespace std;

namespace {
// format family without bit depth and plane order, see VideoInfo::IsYA etc. in the core
inline int format_family(int pixel_type) { return pixel_type & VideoInfo::CS_PLANAR_MASK & ~VideoInfo::CS_Sample_Bits_Mask & VideoInfo::CS_PLANAR_FILTER; }
inline bool is_family(int pixel_type, int generic) { return format_family(pixel_type) == (generic & VideoInfo::CS_PLANAR_FILTER); }
}

bool is_v13_pixel_type(int pixel_type)
{
    if (!(pixel_type & VideoInfo::CS_PLANAR))
    {
        return false;
    }
    if (is_family(pixel_type, VideoInfo::CS_GENERIC_YA) || is_family(pixel_type, VideoInfo::CS_GENERIC_YUV440) || is_family(pixel_type, VideoInfo::CS_GENERIC_YUVA440)
        || is_family(pixel_type, VideoInfo::CS_GENERIC_YUVA411) || is_family(pixel_type, VideoInfo::CS_GENERIC_YUVA410))
    {
        return true;
    }
    // 8 bit 4:1:1 (YV411) and 4:1:0 (YUV9) are old formats
    return (is_family(pixel_type, VideoInfo::CS_GENERIC_YUV411) || is_family(pixel_type, VideoInfo::CS_GENERIC_YUV410))
        && (pixel_type & VideoInfo::CS_Sample_Bits_Mask) != VideoInfo::CS_Sample_Bits_8;
}

int get_transfer_planes(const VideoInfo& vi, int planes[4])
{
    if (!vi.IsPlanar() || vi.IsY())
    {
        planes[0] = PLANAR_Y;
        return 1;
    }
    if (is_family(vi.pixel_type, VideoInfo::CS_GENERIC_YA))
    {
        planes[0] = PLANAR_Y;
        planes[1] = PLANAR_A;
        return 2;
    }
    const bool yuv = vi.IsYUV() || vi.IsYUVA();
    planes[0] = yuv ? PLANAR_Y : PLANAR_G;
    planes[1] = yuv ? PLANAR_U : PLANAR_B;
    planes[2] = yuv ? PLANAR_V : PLANAR_R;
    planes[3] = PLANAR_A;
    return vi.IsYUVA() || vi.IsPlanarRGBA() ? 4 : 3;
}

ClipSyncGroup::ClipSyncGroup(const sys_string& name, int clip_index, shared_memory_clip_info_t& clip_info, bool is_server, ActivitySignal* activity)
{
    for (int i = 0; i < 2; i++)
    {
        sys_ostringstream ss;
        ss << name << SYSTEXT("_") << clip_index << SYSTEXT("_Response") << i;
        response_conds.push_back(unique_ptr<CondVar>(new CondVar(&clip_info.frame_response[i].lock, ss.str(), is_server, activity)));
    }
    if (clip_info.audio_buffer_samples > 0)
    {
        sys_ostringstream ss;
        ss << name << SYSTEXT("_") << clip_index;
        audio_signal.reset(new TwoSidedLock(ss.str().c_str(), SYSTEXT("Audio"), is_server));
    }
}

sys_string get_shared_memory_key(const char* key1, int key2)
{
    if (strlen(key1) > 256)
    {
        throw invalid_argument("key1 is too long.");
    }
    return get_shared_memory_key_prefix(key1) + ascii_to_sys(std::to_string(key2));
}

sys_string get_shared_memory_key_prefix(const char* key1)
{
    return ascii_to_sys(std::string("SharedMemorySource_") + key1 + "_");
}

int get_response_index(int frame_number)
{
    return frame_number & 1;
}


void add_guard_bytes(unsigned char* address, uint32_t buffer_size)
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

void check_guard_bytes(unsigned char* address, uint32_t buffer_size)
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

// Fills the immutable part of a clip info (layout of its buffers in the mapping), from
// mapping_size on; returns the mapping size after the clip's buffers.
static uint32_t compute_clip_layout(shared_memory_clip_info_t& info, const VideoInfo& vi, uint32_t mapping_size)
{
    info.vi = vi;
    uint32_t clip_buffer_size = aligned(info.vi.RowSize()) * info.vi.height;
    info.frame_pitch = aligned(info.vi.RowSize(), FRAME_ALIGN);

    int planes[4];
    const int plane_count = get_transfer_planes(info.vi, planes);
    // planes after the first one: [U, V] or [B, R], then A (YA: only A)
    for (int p = 1; p < plane_count; p++)
    {
      const int plane = planes[p];
      const int row_size = info.vi.RowSize(plane);
      const int height = (plane == PLANAR_U || plane == PLANAR_V)
        ? info.vi.height >> info.vi.GetPlaneHeightSubsampling(plane)
        : info.vi.height;
      const int pitch = aligned(row_size, FRAME_ALIGN);
      if (plane == PLANAR_A)
      {
        info.frame_pitch_a = pitch;
        info.frame_offset_a = clip_buffer_size;
      } else {
        info.frame_pitch_uv = pitch;
        if (plane == PLANAR_U || plane == PLANAR_B)
          info.frame_offset_u = clip_buffer_size;
        else
          info.frame_offset_v = clip_buffer_size;
      }
      clip_buffer_size += pitch * height;
    }
    info.frame_buffer_size = clip_buffer_size;

    // add some extra space before and after the buffer for guard bytes
    info.frame_buffer_offset[0] = mapping_size + CACHE_LINE_SIZE * 2;
    mapping_size = aligned(mapping_size + clip_buffer_size + 2048);

    info.frame_buffer_offset[1] = mapping_size + CACHE_LINE_SIZE * 2;
    mapping_size = aligned(mapping_size + clip_buffer_size + 2048);

    // inline frame property areas, with the same guard byte spacing
    for (int i = 0; i < 2; i++)
    {
        info.props_inline_offset[i] = mapping_size + CACHE_LINE_SIZE * 2;
        mapping_size = aligned(mapping_size + PROPS_INLINE_SIZE + 2048);
    }

    // audio buffer of a whole number of samples, with the same guard byte spacing
    info.audio_buffer_offset = 0;
    info.audio_buffer_samples = 0;
    if (info.vi.HasAudio())
    {
        const int bytes_per_sample = info.vi.BytesPerAudioSample();
        if (bytes_per_sample <= 0)
        {
            throw runtime_error("Unsupported audio format.");
        }
        info.audio_buffer_samples = AUDIO_BUFFER_SIZE / (uint32_t)bytes_per_sample;
        info.audio_buffer_offset = mapping_size + CACHE_LINE_SIZE * 2;
        mapping_size = aligned(mapping_size + info.audio_buffer_samples * (uint32_t)bytes_per_sample + 2048);
    }
    return mapping_size;
}

static uint32_t audio_buffer_bytes(const shared_memory_clip_info_t& info)
{
    return info.audio_buffer_samples * (uint32_t)info.vi.BytesPerAudioSample();
}

void SharedMemorySourceManager::init_server(const SYSCHAR* mapping_name, int clip_count, const VideoInfo vi_array[])
{
    assert(clip_count > 0);

    // the clip infos are part of the header, the frame buffers follow them
    const uint32_t header_size = aligned((uint32_t)(sizeof(shared_memory_source_header_t) + sizeof(shared_memory_clip_info_t) * clip_count));

    // 1st pass: the size of the mapping (the layout is computed again into the mapping)
    uint32_t mapping_size = header_size;
    for (int clip_i = 0; clip_i < clip_count; clip_i++)
    {
        shared_memory_clip_info_t scratch;
        mapping_size = compute_clip_layout(scratch, vi_array[clip_i], mapping_size);
    }

    switch (_mapping.create(mapping_name, mapping_size))
    {
    case platform::SharedMapping::OK:
        break;
    case platform::SharedMapping::ALREADY_EXISTS:
        throw runtime_error("The file mapping object already exists.");
    case platform::SharedMapping::VIEW_FAILED:
        assert(false);
        throw runtime_error("MapViewOfFile failed.");
    default:
        assert(false);
        throw runtime_error("Unable to create file mapping object.");
    }
    header = (shared_memory_source_header_t*)_mapping.data();

    // a new mapping is zero filled: every atomic, lock and counter starts as 0
    header->signature = SHARED_MEMORY_SOURCE_SIGNATURE;
    header->clip_count = clip_count;
    header->server_process_id = platform::current_process_id();
    uint32_t offset = header_size;
    for (int i = 0; i < clip_count; i++)
    {
        shared_memory_clip_info_t& info = header->clips[i];
        offset = compute_clip_layout(info, vi_array[i], offset);
        info.frame_response[0].frame_number = -1;
        info.frame_response[1].frame_number = -1;
        for (int j = 0; j < 2; j++)
        {
            add_guard_bytes(frame_buffer(i, j), info.frame_buffer_size);
            add_guard_bytes(props_inline_area(i, j), PROPS_INLINE_SIZE);
        }
        if (info.audio_buffer_samples > 0)
        {
            add_guard_bytes(audio_buffer(i), audio_buffer_bytes(info));
        }
    }
    assert(offset == mapping_size);
}

void SharedMemorySourceManager::check_audio_buffer_integrity(int clip_index)
{
    assert(clip_index >= 0 && clip_index < header->clip_count);
    assert(header->clips[clip_index].audio_buffer_samples > 0);
    check_guard_bytes(audio_buffer(clip_index), audio_buffer_bytes(header->clips[clip_index]));
}


void SharedMemorySourceManager::check_data_buffer_integrity(int clip_index, int response_object_id)
{
    assert(clip_index >= 0 && clip_index < header->clip_count);
    assert(response_object_id >= 0 && response_object_id <= 1);
    check_guard_bytes(frame_buffer(clip_index, response_object_id), header->clips[clip_index].frame_buffer_size);
    check_guard_bytes(props_inline_area(clip_index, response_object_id), PROPS_INLINE_SIZE);
}

void SharedMemorySourceManager::signal_shutdown()
{
    if (_is_server)
    {
        while (true)
        {
            object_state_t state = load_object_state(header);
            if (state.shutdown)
            {
                break;
            }
            int32_t old_state_value = state.state_value;
            state.shutdown = 1;
            state.sequence_number++;
            if (header->object_state.compare_exchange_strong(old_state_value, state.state_value))
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
            if (sync_groups[i]->audio_signal)
            {
                sync_groups[i]->audio_signal->signal_all();
            }
        }
    }
}

void SharedMemorySourceManager::init_client(const SYSCHAR* mapping_name)
{
    switch (_mapping.open(mapping_name, true))
    {
    case platform::SharedMapping::OK:
        break;
    case platform::SharedMapping::VIEW_FAILED:
        assert(false);
        throw runtime_error("MapViewOfFile failed.");
    default:
        assert(false);
        throw runtime_error("Unable to open the file mapping object, maybe the server is closed.");
    }
    header = (shared_memory_source_header_t*)_mapping.data();
    if (header->signature != SHARED_MEMORY_SOURCE_SIGNATURE)
    {
        assert(false);
        throw runtime_error("Invalid shared memory object (an MP_Pipeline.dll and its .slave.exe / .win32 / .win64 files from different versions?).");
    }
    const uint32_t process_id = platform::current_process_id();
    for (int i = 0; i < MAX_SLAVES; i++)
    {
        uint32_t free_slot = 0;
        if (header->client_process_ids[i].compare_exchange_strong(free_slot, process_id))
        {
            break;
        }
    }
}

void SharedMemorySourceManager::init_sync_objects(const sys_string& key, int clip_count)
{
    activity = unique_ptr<ActivitySignal>(new ActivitySignal(key + SYSTEXT("_Activity"), header->activity, _is_server));

    sys_string cond_event_name(key);
    cond_event_name.append(SYSTEXT("_CondEvent"));
    request_cond = unique_ptr<CondVar>(new CondVar(&header->request_lock, cond_event_name, _is_server, activity.get()));

    parity_signal = unique_ptr<TwoSidedLock>(new TwoSidedLock(key.c_str(), SYSTEXT("ParitySignal"), _is_server));

    if (_is_server)
    {
        request_cond->signal.switch_to_other_side();
    }
    for (int i = 0; i < clip_count; i++)
    {
        sync_groups.push_back(unique_ptr<ClipSyncGroup>(new ClipSyncGroup(key, i, header->clips[i], _is_server, activity.get())));
    }
    if (!_is_server)
    {
        // a killed or crashed server would leave the client waiting forever
        const uint32_t server = header->server_process_id;
        request_cond->signal.watch_peer(server);
        parity_signal->watch_peer(server);
        for (auto& group : sync_groups)
        {
            for (auto& cond : group->response_conds)
            {
                cond->signal.watch_peer(server);
            }
            if (group->audio_signal)
            {
                group->audio_signal->watch_peer(server);
            }
        }
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
    header(NULL),
    key(key)
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
    header(NULL),
    key(key)
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
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            bool has_alive_client = false;
            for (int i = 0; i < MAX_SLAVES; i++)
            {
                const uint32_t client_process_id = header->client_process_ids[i];
                if (client_process_id && platform::is_process_alive(client_process_id))
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
        } while (load_object_state(header).client_count > 0);
    } else {
        const uint32_t process_id = platform::current_process_id();
        for (int i = 0; i < MAX_SLAVES; i++)
        {
            uint32_t own_slot = process_id;
            if (header->client_process_ids[i].compare_exchange_strong(own_slot, 0))
            {
                break;
            }
        }
    }
    header = NULL;
    _mapping.close();
}

void SharedMemorySourceClientLockAcquire::do_lock(short increment)
{
    while (true)
    {
        object_state_t state = load_object_state(_manager.header);
        int32_t old_state_value = state.state_value;
        state.sequence_number++;
        state.client_count += increment;
        if (_manager.header->object_state.compare_exchange_strong(old_state_value, state.state_value))
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