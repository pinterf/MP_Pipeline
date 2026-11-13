#include "common.h"
#define SHAREDMEMORYSERVER_SIMPLE_MACRO_NAME
#define SHAREDMEMORYSERVER_IMPLEMENTATION
#include "SharedMemoryServer.h"

// #define ENABLE_TRACING
#define TRACE_PREFIX L"SharedMemoryServer"
#include "trace.h"
#include "utils.h"
#include "slave_common.h"
#include <assert.h>
#include <chrono>
#include <system_error>
#include <exception>
#include <memory>
#include <sstream>

using namespace std;

AVSValue __cdecl Create_SharedMemoryServer(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    SharedMemoryServer_parameter_storage_t params(args);
    AVSValue aux_clips = ARG(aux_clips);
    if (!aux_clips.Defined()) // a named [aux_clips]c* array can be undefined and not zero sized if not given since 3.7.1test17, prepare for that
      aux_clips = AVSValue(NULL, 0); // zero sized intentionally
    assert(aux_clips.IsArray());
    int array_size = aux_clips.ArraySize();
    if (array_size > MAX_EXPORTED_CLIPS)
    {
        // MP_Pipeline checks it per script block already, this is for direct use
        env->ThrowError("SharedMemoryServer: Each process can only export no more than %d clips.", MAX_EXPORTED_CLIPS);
    }
    unique_ptr<PClip[]> clips(new PClip[array_size + 2]);
    clips[0] = ARG(child).AsClip();
    for (int i = 0; i < array_size; i++)
    {
        clips[i + 1] = aux_clips[i].AsClip();
    }
    clips[array_size + 1] = NULL;
    unique_ptr<VideoInfo[]> vi_array(new VideoInfo[array_size + 1]);
    for (int i = 0; i < array_size + 1; i++)
    {
        vi_array[i] = clips[i]->GetVideoInfo();
        // disable audio
        vi_array[i].audio_samples_per_second = 0;
    }
    try
    {
        return new SharedMemoryServer(clips.get(), array_size + 1, vi_array.get(), params, env);
    } catch (AvisynthError&) {
        // no need to process it here
        throw;
    } catch (std::exception& e) {
        env->ThrowError("SharedMemoryServer: %s", e.what());
    } catch (...) {
        env->ThrowError("SharedMemoryServer: Unknown exception");
    }
    return AVSValue(); // not reached: ThrowError throws (it is not declared noreturn)
}

bool SharedMemoryServer::is_shutting_down() const
{
    return _shutdown || load_object_state(_manager.header).shutdown;
}


void trace_avs_error(AvisynthError& e)
{
    TRACE("AvisynthError: %hs", e.msg);
}

void SharedMemoryServer::process_get_parity(const shared_memory_source_request_t& request)
{
    TwoSidedLockContext ctx(*_manager.parity_signal);

    int response_index = get_response_index(request.frame_number);
    std::atomic<int64_t>& result_reference = _manager.header->clips[request.clip_index].parity_response[response_index];
    parity_response_t result;

    uint8_t parity = 0;

    while (true)
    {
        if (is_shutting_down())
        {
            return;
        }
        result.value = result_reference.load();
        if (result.frame_number == request.frame_number && (result.parity & 0x80) != 0)
        {
            auto new_result = result;
            new_result.sequence_number++;
            new_result.requested_client_count++;
            int64_t expected = result.value;
            if (result_reference.compare_exchange_strong(expected, new_result.value))
            {
                return;
            } else {
                Sleep(0);
                continue;
            }
        }

        assert(result.requested_client_count >= 0);
        if (result.requested_client_count > 0)
        {
            Sleep(0);
            continue;
        }

        if ((parity & 0x80) == 0)
        {
            parity = 0x80 | (_fetcher.GetParity(request.clip_index, request.frame_number) ? 1 : 0);
        }

        auto new_result = result;
        new_result.sequence_number++;
        new_result.frame_number = request.frame_number;
        new_result.requested_client_count++;
        new_result.parity = parity;
        int64_t expected = result.value;
        if (result_reference.compare_exchange_strong(expected, new_result.value))
        {
            return;
        } else {
            Sleep(0);
            continue;
        }
    }
}

bool can_prefetch(const frame_response_t& response)
{
    return response.requested_client_count == 0 && (!response.is_prefetch || response.prefetch_hit > 0);
}

bool SharedMemoryServer::try_prefetch_frame()
{
    for (int clip_index = 0; clip_index < _manager.header->clip_count; clip_index++)
    {
        // we don't need to lock the response now, 
        // because the client won't modify frame number of it,
        // other parts don't matter
        auto& clip = _manager.header->clips[clip_index];
        int resp_index = 0;
        if (!can_prefetch(clip.frame_response[0]))
        {
            if (can_prefetch(clip.frame_response[1]))
            {
                resp_index = 1;
            } else {
                continue;
            }
        } else {
            if (can_prefetch(clip.frame_response[1]) && clip.frame_response[1].frame_number < clip.frame_response[0].frame_number)
            {
                resp_index = 1;
            }
        }
        int alternate_index = resp_index == 0 ? 1 : 0;
        if (clip.frame_response[alternate_index].frame_number - clip.frame_response[resp_index].frame_number != 1)
        {
            // not sure whether the client is requesting linearly, don't prefetch now
            continue;
        }
        auto& resp = clip.frame_response[resp_index];
        if (resp.is_prefetch && resp.prefetch_hit > 0)
        {
            // tell the fetcher to continue prefetching
            _fetcher.set_last_requested_frame(clip_index, resp.frame_number, true);

            // reset the flag so we won't re-enter here next time
            // don't lock here, since only our thread will access this flag
            resp.is_prefetch = false;
        }
        // remember we picked the response with smaller frame number
        int next_frame_number = resp.frame_number + 2;

        if (next_frame_number >= clip.vi.num_frames)
        {
            continue;
        }
        PVideoFrame frame = _fetcher.try_get_frame_from_cache(clip_index, next_frame_number);
        if (!frame)
        {
            TRACE("(Clip %d) Frame %d is not in fetcher cache", clip_index, next_frame_number);
            continue;
        }
        auto& cond = *_manager.sync_groups[clip_index]->response_conds[resp_index];
        if (!cond.lock.try_lock())
        {
            continue;
        }
        {
            SpinLockContext<> ctx(cond.lock);
            // recheck for safe
            if (!can_prefetch(resp))
            {
                continue;
            }
            TRACE("(Clip %d) Prefetched frame buffer: %d", clip_index, next_frame_number);
            resp.frame_number = next_frame_number;
            resp.is_prefetch = true;
            resp.prefetch_hit = 0;
            assert(resp.requested_client_count == 0);
            copy_frame(frame, clip_index, resp_index);
        }
        cond.signal.switch_to_other_side();
        return true;
    }
    return false;
}

void SharedMemoryServer::copy_frame(PVideoFrame frame, int clip_index, int response_index)
{
    auto& clip = _manager.header->clips[clip_index];
    unsigned char* buffer = _manager.frame_buffer(clip_index, response_index);

    int planes[4];
    const int plane_count = get_transfer_planes(clip.vi, planes);
    for (int p = 0; p < plane_count; p++)
    {
        copy_plane(buffer + plane_buffer_offset(clip, planes[p]), plane_buffer_pitch(clip, planes[p]), frame, clip.vi, planes[p]);
    }

    _props_writers[clip_index * 2 + response_index]->write(_env, _props_caps, frame,
        clip.frame_response[response_index].props, _manager.props_inline_area(clip_index, response_index));

    _manager.check_data_buffer_integrity(clip_index, response_index);
}

void SharedMemoryServer::process_get_frame(const shared_memory_source_request_t& request)
{
    TRACE("(Clip %d) process_get_frame: %d", request.clip_index, request.frame_number);
    int response_index = get_response_index(request.frame_number);
    assert(response_index >= 0 && response_index < 2);
    CondVar& cond = *_manager.sync_groups[request.clip_index]->response_conds[response_index];
    auto& clip = _manager.header->clips[request.clip_index];
    auto& response = clip.frame_response[response_index];

    // since only this thread can modify the response, don't need locking here
    if (response.frame_number == request.frame_number)
    {
        TRACE("(Clip %d) process_get_frame: %d (Already in response)", request.clip_index, request.frame_number);
        response.requested_client_count++;
        cond.signal.switch_to_other_side();
        return;
    }

    PVideoFrame frame = _fetcher.GetFrame(request.clip_index, request.frame_number, _env);
    if (!frame)
    {
        throw runtime_error("Unable to fetch frame");
    }

    while (true)
    {
        {
            CondVarLockAcquire cvla(cond, CondVarLockAcquire::LOCK_LONG);
            if (is_shutting_down())
            {
                cond.signal.signal_all();
                return;
            }
            assert(response.requested_client_count >= 0);
            cvla.signal_after_unlock = true;
            if (response.requested_client_count == 0)
            {
                if (response.is_prefetch)
                {
                    TRACE("(Clip %d) Replacing prefetched frame %d (hit: %d) with frame %d", request.clip_index, response.frame_number.load(), response.prefetch_hit.load(), request.frame_number);
                }
                response.frame_number = request.frame_number;
                // the client count is atomic, the client decrements it
                response.requested_client_count = 1;
                response.is_prefetch = false;
                response.prefetch_hit = 0;
                copy_frame(frame, request.clip_index, response_index);
                TRACE("(Clip %d) process_get_frame: %d (Complete)", request.clip_index, request.frame_number);
                break;
            }
        }
        // a client requested a frame, but not fetched yet.
        // don't wait indefinitely here to prevent deadlock with multiple client.

        // 5ms should be OK, it shouldn't take more than 1ms for a client to copy out 
        // the frame and switch back to server, so it is long enough.

        // when clients are deadlocked, 5ms recovery time should be OK for processing at 100 FPS 
        cond.signal.wait_on_this_side(5);
    }
    TRACE("(Clip %d) process_get_frame: %d (End)", request.clip_index, request.frame_number);
}

unsigned SharedMemoryServer::thread_proc()
{
    shared_memory_source_request_t request;
    while (!is_shutting_down())
    {
        request.request_type = REQ_EMPTY;

        {
            CondVarLockAcquire cvla(*_manager.request_cond, CondVarLockAcquire::LOCK_SHORT);
            if (is_shutting_down())
            {
                _manager.request_cond->signal.signal_all();
                break;
            }
            cvla.signal_after_unlock = true;
            auto& slot = _manager.header->request;
            if (slot.request_type != REQ_EMPTY)
            {
                request.request_type = (request_type_t)slot.request_type.load();
                request.clip_index = slot.clip_index;
                request.frame_number = slot.frame_number;
                request.sequence_number = slot.sequence_number;
                slot.request_type = REQ_EMPTY;
                slot.sequence_number++;
            }
        }

        if (request.request_type == REQ_EMPTY)
        {
            for (auto it = _manager.sync_groups.begin(); it < _manager.sync_groups.end(); it++) {
                auto& conds = (*it)->response_conds;
                for (auto cond_it = conds.begin(); cond_it < conds.end(); cond_it++) {
                    (*cond_it)->signal.switch_to_other_side();
                }
            }
            if (try_prefetch_frame())
            {
                continue;
            }
            wait_for_activity();
            continue;
        }
        bool success = false;
        try
        {
            switch (request.request_type)
            {
            case REQ_GETFRAME:
                process_get_frame(request);
                break;
            case REQ_GETPARITY:
                process_get_parity(request);
                break;
            default:
                assert(false);
                throw std::invalid_argument("Invalid request.");
            }
            success = true;
        } catch (AvisynthError& e) {
            strncpy(_manager.header->error_msg, e.msg, ARRAYSIZE(_manager.header->error_msg));
            trace_avs_error(e);
        } catch (runtime_error& e) {
            strncpy(_manager.header->error_msg, e.what(), ARRAYSIZE(_manager.header->error_msg));
            TRACE("Runtime error while handling request: %hs", e.what());
        } catch (...) {
            strncpy(_manager.header->error_msg, "Unknown error.", ARRAYSIZE(_manager.header->error_msg));
            TRACE("Unknown error occurred while handling request.");
            assert(false);
            __debugbreak();
        }
        if (!success)
        {
            initiate_shutdown();
            return 1;
        }
    }
    initiate_shutdown();
    return 0;
}

void SharedMemoryServer::initiate_shutdown()
{
    _shutdown = true;
    _manager.signal_shutdown();
    _fetcher.signal_shutdown();
}

void SharedMemoryServer::wait_for_activity()
{
    assert(_activity_wait_handles.size() > 0 && _activity_wait_handles.size() < MAXIMUM_WAIT_OBJECTS);
    DWORD result = WaitForMultipleObjectsEx((DWORD)_activity_wait_handles.size(), &_activity_wait_handles[0], FALSE, INFINITE, TRUE);
    if (result == WAIT_IO_COMPLETION)
    {
        return;
    } else if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + MAXIMUM_WAIT_OBJECTS) {
        int index = result - WAIT_OBJECT_0;
        assert(index < (int)_activity_wait_handles.size());
        ResetEvent(_activity_wait_handles[index]);
        return;
    } else {
        DWORD code = -1;
        if (result == WAIT_FAILED)
        {
            code = GetLastError();
        }
        assert(false);
        ostringstream ss;
        ss << "Unexpected error: wait failed, result: " << result << ", code: " << code;
        throw runtime_error(ss.str());
    }
}

SharedMemoryServer::SharedMemoryServer(const PClip clips[], int clip_count, const VideoInfo vi_array[], const SharedMemoryServer_parameter_storage_t& o, IScriptEnvironment* env):
    SharedMemoryServer_parameter_storage_t(o),
    GenericVideoFilter(clips[0]),
    _env(env),
    _shutdown(false),
    _thread_exited(false),
    _fetcher(clips, _max_cache_frames, _cache_behind, env, _fetcher_thread_lock_to_cpu),
    _manager(get_shared_memory_key(_instance_key, _port), clip_count, vi_array),
    _props_caps(detect_frame_props_caps(env))
{
    if (!_props)
    {
        // "### frame props: off": the slot never carries properties, downstream frames get none
        _props_caps.has_props = false;
    }
    for (int i = 0; i < clip_count; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            _props_writers.push_back(std::unique_ptr<PropsWriter>(new PropsWriter(_manager.key, i, j)));
        }
    }

    _activity_wait_handles.push_back(_fetcher.new_frame_in_cache_event.get());

    _activity_wait_handles.push_back(_manager.request_cond->signal.get_event_this_side());
    for (int i = 0; i < clip_count; i++)
    {
        _activity_wait_handles.push_back(_manager.sync_groups[i]->response_conds[0]->signal.get_event_this_side());
        _activity_wait_handles.push_back(_manager.sync_groups[i]->response_conds[1]->signal.get_event_this_side());
    }

    try
    {
        _thread = std::thread([this] {
            // a failed affinity is ignored: the server works without it
            lock_current_thread_to_cpu(_server_thread_lock_to_cpu);
            thread_proc();
            _thread_exited = true;
        });
    } catch (const std::system_error&) {
        env->ThrowError("SharedMemoryServer: Unable to create thread.");
    }
}

SharedMemoryServer::~SharedMemoryServer()
{
    initiate_shutdown();
    // repeat the shutdown signal until the thread has seen it (it may be waiting for activity)
    while (_thread.joinable() && !_thread_exited)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        initiate_shutdown();
    }
    if (_thread.joinable())
    {
        _thread.join();
    }
}

PVideoFrame SharedMemoryServer::GetFrame(int n, IScriptEnvironment* env)
{
    return _fetcher.GetFrame(0, n, env);
}

bool SharedMemoryServer::GetParity(int n)
{
    return _fetcher.GetParity(0, n);
}