#include "stdafx.h"
#define SHAREDMEMORYSERVER_SIMPLE_MACRO_NAME
#define SHAREDMEMORYSERVER_IMPLEMENTATION
#include "SharedMemoryServer.h"

// #define ENABLE_TRACING
#define TRACE_PREFIX L"SharedMemoryServer"
#include "trace.h"
#include "utils.h"
#include <assert.h>
#include <process.h>
#include <exception>
#include <memory>
#include <sstream>

using namespace std;

AVSValue __cdecl Create_SharedMemoryServer(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    SharedMemoryServer_parameter_storage_t params(args);
    AVSValue aux_clips = ARG(aux_clips);
    assert(aux_clips.IsArray());
    int array_size = aux_clips.ArraySize();
    if (array_size > 8)
    {
        env->ThrowError("SharedMemoryServer: Each process can only export no more than 8 clips.");
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
}

bool SharedMemoryServer::is_shutting_down() const
{
    return _shutdown || _manager.header->object_state.shutdown;
}

unsigned __stdcall SharedMemoryServer::thread_stub(void* self)
{
    return ((SharedMemoryServer*)self)->thread_proc();
}

void trace_avs_error(AvisynthError& e)
{
    TRACE("AvisynthError: %hs", e.msg);
}

void SharedMemoryServer::process_get_parity(const shared_memory_source_request_t& request)
{
    TwoSidedLockContext ctx(*_manager.parity_signal);

    int response_index = get_response_index(request.frame_number);
    volatile parity_response_t& result_reference = _manager.header->clips[request.clip_index].parity_response[response_index];
    parity_response_t result;

    char parity = 0;

    while (true)
    {
        if (is_shutting_down())
        {
            return;
        }
        result.value = _InterlockedCompareExchange64(&result_reference.value, 0, 0);
        if (result.frame_number == request.frame_number && (result.parity & 0x80) != 0)
        {
            auto new_result = result;
            new_result.sequence_number++;
            new_result.requested_client_count++;
            if (_InterlockedCompareExchange64(&result_reference.value, new_result.value, result.value) == result.value)
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
        if (_InterlockedCompareExchange64(&result_reference.value, new_result.value, result.value) == result.value)
        {
            return;
        } else {
            Sleep(0);
            continue;
        }
    }
}

bool can_prefetch(const volatile frame_response_t& response)
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
        volatile auto& resp = clip.frame_response[resp_index];
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
    const auto& clip = _manager.header->clips[clip_index];
    unsigned char* buffer = (unsigned char*)_manager.header + clip.frame_buffer_offset[response_index];

    const int planesYUV[4] = { PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A };
    const int planesRGB[4] = { PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A };
    const int* planes = clip.vi.IsYUV() || clip.vi.IsYUVA() ? planesYUV : planesRGB;

    copy_plane(buffer, clip.frame_pitch, frame, clip.vi, planes[0]); // Y or G
    if (clip.vi.IsPlanar() && !clip.vi.IsY())
    {
      copy_plane(buffer + clip.frame_offset_u, clip.frame_pitch_uv, frame, clip.vi, planes[1]); // U or B
      copy_plane(buffer + clip.frame_offset_v, clip.frame_pitch_uv, frame, clip.vi, planes[2]); // V or R
      if (clip.vi.NumComponents() == 4)
        copy_plane(buffer + clip.frame_offset_a, clip.frame_pitch_a, frame, clip.vi, planes[3]); // A
    }

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
        _InterlockedIncrement(&response.requested_client_count);
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
                    TRACE("(Clip %d) Replacing prefetched frame %d (hit: %d) with frame %d", request.clip_index, response.frame_number, response.prefetch_hit, request.frame_number);
                }
                response.frame_number = request.frame_number;
                // note: use Interlocked functions if need to change client count in other place!
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
            if (_manager.header->request.request_type != REQ_EMPTY)
            {
                memcpy(&request, (const void*)&(_manager.header->request), sizeof(request));
                _manager.header->request.request_type = REQ_EMPTY; 
                _manager.header->request.sequence_number++;
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

SharedMemoryServer::SharedMemoryServer(const PClip clips[], int clip_count, const VideoInfo vi_array[], SharedMemoryServer_parameter_storage_t& o, IScriptEnvironment* env):
    SharedMemoryServer_parameter_storage_t(o),
    GenericVideoFilter(clips[0]),
    _thread_handle(NULL),
    _env(env),
    _shutdown(false),
    _fetcher(clips, _max_cache_frames, _cache_behind, env),
    _manager(get_shared_memory_key(_instance_key, _port), clip_count, vi_array)
{
    _activity_wait_handles.push_back(_fetcher.new_frame_in_cache_event.get());

    _activity_wait_handles.push_back(_manager.request_cond->signal.get_event_this_side());
    for (int i = 0; i < clip_count; i++)
    {
        _activity_wait_handles.push_back(_manager.sync_groups[i]->response_conds[0]->signal.get_event_this_side());
        _activity_wait_handles.push_back(_manager.sync_groups[i]->response_conds[1]->signal.get_event_this_side());
    }

    _thread_handle.replace((HANDLE)_beginthreadex(NULL, 0, thread_stub, this, 0, 0));
    if (!_thread_handle.is_valid())
    {
        env->ThrowError("SharedMemoryServer: Unable to create thread.");
    }
    if (_fetcher_thread_lock_to_cpu >= 0)
    {
        if (!_fetcher.set_worker_thread_affinity( ((DWORD_PTR)1) << _fetcher_thread_lock_to_cpu ))
        {
            // Don't crash if affinity is not met
            // env->ThrowError("SharedMemoryServer: Unable to set affinity of fetcher thread, code = %d", GetLastError());
        }
    }
    if (_server_thread_lock_to_cpu >= 0)
    {
        if (!SetThreadAffinityMask(_thread_handle.get(), ((DWORD_PTR)1) << _server_thread_lock_to_cpu ))
        {
            // Don't crash if affinity is not met
            // env->ThrowError("SharedMemoryServer: Unable to set affinity of server thread, code = %d", GetLastError());
        }
    }
}

SharedMemoryServer::~SharedMemoryServer()
{
    initiate_shutdown();
    while (_thread_handle.wait(10) == WAIT_TIMEOUT)
    {
        initiate_shutdown();
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