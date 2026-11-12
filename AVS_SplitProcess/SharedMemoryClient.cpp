#include "stdafx.h"
#define SHAREDMEMORYCLIENT_SIMPLE_MACRO_NAME
#define SHAREDMEMORYCLIENT_IMPLEMENTATION
#include "SharedMemoryClient.h"
#include "SafeEnv.h"
#include "utils.h"

// #define ENABLE_TRACING
#define TRACE_PREFIX L"SharedMemoryClient"
#include "trace.h"
#include <assert.h>
#include <stdexcept>

using namespace std;


AVSValue __cdecl Create_SharedMemoryClient(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    try
    {
        return new SharedMemoryClient(SharedMemoryClient_parameter_storage_t(args), env);
    } catch (AvisynthError&) {
        // no need to process it here
        throw;
    } catch (std::exception& e) {
        env->ThrowError("SharedMemoryClient: %s", e.what());
    } catch (...) {
        env->ThrowError("SharedMemoryClient: Unknown exception");
    }
}

SharedMemoryClient::SharedMemoryClient(const SharedMemoryClient_parameter_storage_t& o, IScriptEnvironment* env) :
    SharedMemoryClient_parameter_storage_t(o),
    _shutdown(false),
    _manager(get_shared_memory_key(_instance_key, _port)),
    _vi(_manager.header->clips[_clip_index].vi),
    _props_caps(detect_frame_props_caps(env))
{
    check_shutting_down(env);
    if (is_v13_pixel_type(_vi.pixel_type))
    {
        // the VideoInfo functions of an older core don't know the format (and V13 linkage
        // entries like VideoInfo::IsYA are missing there)
        bool supported = true;
        try
        {
            env->CheckVersion(13);
        } catch (const AvisynthError&) {
            supported = false;
        }
        if (!supported)
        {
            env->ThrowError("SharedMemoryClient: the upstream clip format (YA, 4:4:0, or 4:1:1 / 4:1:0 with alpha or more than 8 bits) "
                "needs AviSynth+ interface V13 or later in this process.");
        }
    }
    // we don't support audio
    _vi.audio_samples_per_second = 0;
    for (int i = 0; i < 2; i++)
    {
        _props_readers[i].reset(new PropsReader(_manager.key, _clip_index, i));
    }
}

void SharedMemoryClient::throw_if_props_error(const std::string& props_error, IScriptEnvironment* env)
{
    if (!props_error.empty())
    {
        env->ThrowError("SharedMemoryClient: frame properties: %s", props_error.c_str());
    }
}

SharedMemoryClient::~SharedMemoryClient()
{
    _shutdown = true;
}

bool SharedMemoryClient::is_shutting_down()
{
    return _manager.header->object_state.shutdown || _shutdown;
}

void SharedMemoryClient::check_shutting_down(IScriptEnvironment* env)
{
    if (is_shutting_down())
    {
        _manager.request_cond->signal.signal_all();
        if (_manager.header->error_msg[0] != 0)
        {
            env->ThrowError("SharedMemoryClient: Server got error: %s", _manager.header->error_msg);
        } else {
            env->ThrowError("SharedMemoryClient: The server has been shut down.");
        }
    }
}

PVideoFrame SharedMemoryClient::create_frame(int response_index, IScriptEnvironment* env, std::string& props_error)
{
    auto& clip = _manager.header->clips[_clip_index];
    const unsigned char* buffer = _manager.frame_buffer(_clip_index, response_index);
    PVideoFrame frame = SafeNewVideoFrame(env, _vi);

    int planes[4];
    const int plane_count = get_transfer_planes(_vi, planes);
    for (int p = 0; p < plane_count; p++)
    {
        copy_plane(frame, buffer + plane_buffer_offset(clip, planes[p]), plane_buffer_pitch(clip, planes[p]), _vi, planes[p]);
    }
    try
    {
        _props_readers[response_index]->read(env, _props_caps, frame,
            clip.frame_response[response_index].props, _manager.props_inline_area(_clip_index, response_index));
    } catch (const std::exception& e) {
        props_error = e.what();
    } catch (const AvisynthError& e) {
        props_error = e.msg;
    }
    return frame;
}

PVideoFrame SharedMemoryClient::GetFrame(int n, IScriptEnvironment* env)
{
    SharedMemorySourceClientLockAcquire lock(_manager);
    check_shutting_down(env);
    volatile auto& request = _manager.header->request;
    int response_index = get_response_index(n);
    volatile auto& resp = _manager.header->clips[_clip_index].frame_response[response_index];
    CondVar& cond = *_manager.sync_groups[_clip_index]->response_conds[response_index];
    PVideoFrame frame = NULL;
    std::string props_error;
    if (cond.lock.try_lock(SPIN_LOCK_UNIT * 5))
    {
        SpinLockContext<> ctx(cond.lock);
        check_shutting_down(env);
        if (resp.frame_number == n)
        {
            // prefetch hit
            // don't change client count here since we didn't requested it
            TRACE("(Clip %d) Prefetch hit: %d", _clip_index, n);
            resp.prefetch_hit++;
            frame = create_frame(response_index, env, props_error);
        }
    }
    if (frame)
    {
        cond.signal.switch_to_other_side();
        throw_if_props_error(props_error, env);
        return frame;
    }
    int request_seq = 0;
    while (true)
    {
        {
            CondVarLockAcquire cvla(*_manager.request_cond, CondVarLockAcquire::LOCK_SHORT);
            check_shutting_down(env);
            cvla.signal_after_unlock = true;
            if (request.request_type == REQ_EMPTY)
            {
                request.request_type = REQ_GETFRAME;
                request.clip_index = _clip_index;
                request.frame_number = n;
                request.sequence_number++;
                request_seq = request.sequence_number;
                break;
            }
        }
        _manager.request_cond->signal.wait_on_this_side(INFINITE);
    }
    while (true)
    {
        {
            CondVarLockAcquire cvla(cond, CondVarLockAcquire::LOCK_LONG);
            check_shutting_down(env);
            cvla.signal_after_unlock = true;
            if (resp.frame_number != n)
            {
                // do nothing, just notify the server
                // a. if server is fetching frame, it won't be disturbed
                // b. if server is waiting for another client to fetch its frame, 
                //    server will be woken up and check the state, it will notify the client
                //    again if the response is not ready, preventing deadlock
            } else {
                if (resp.is_prefetch)
                {
                    // prefetched after request
                    resp.prefetch_hit += 100001;
                }
                frame = create_frame(response_index, env, props_error);
                bool server_got_our_request = true;

                if (request.sequence_number == request_seq)
                {
                    CondVarLockAcquire request_lock(*_manager.request_cond, CondVarLockAcquire::LOCK_SHORT);
                    if (request.sequence_number == request_seq)
                    {
                        // we got the prefetched frame before server get our request,
                        // so we just retreat our request
                        assert(request.request_type == REQ_GETFRAME);
                        assert(request.clip_index == _clip_index);
                        assert(request.frame_number == n);
                        request.request_type = REQ_EMPTY;
                        request.sequence_number++;

                        server_got_our_request = false;
                    }
                }
                if (server_got_our_request)
                {
                    _InterlockedDecrement(&resp.requested_client_count);
                } else {
                    // since server doesn't get our request, 
                    // we must NOT decrease the count
                    // and other client can issue request now
                    _manager.request_cond->signal.stay_on_this_side();
                }
                // bookkeeping is complete, the slot locks are released by the scope guards
                throw_if_props_error(props_error, env);
                return frame;
            }
        }
        // there is a race condition here if multiple clients are connected to the same server
        // it is handled on server side
        cond.signal.wait_on_this_side(INFINITE);
    }
}

bool SharedMemoryClient::GetParity(int n)
{
    SharedMemorySourceClientLockAcquire lock(_manager);
    volatile auto& request = _manager.header->request;
    volatile parity_response_t& result_reference = _manager.header->clips[_clip_index].parity_response[get_response_index(n)];
    while (true)
    {
        {
            CondVarLockAcquire cvla(*_manager.request_cond, CondVarLockAcquire::LOCK_SHORT);
            if (is_shutting_down())
            {
                _manager.request_cond->signal.signal_all();
                return false;
            }
            cvla.signal_after_unlock = true;
            if (request.request_type == REQ_EMPTY)
            {
                request.request_type = REQ_GETPARITY;
                request.clip_index = _clip_index;
                request.frame_number = n;
                request.sequence_number++;
                break;
            }
        }
        _manager.request_cond->signal.wait_on_this_side(INFINITE);
    }
    parity_response_t result;
    while (true)
    {
        if (is_shutting_down())
        {
            _manager.request_cond->signal.signal_all();
            return false;
        }
        result.value = _InterlockedCompareExchange64(&result_reference.value, 0, 0);
        if (result.frame_number != n || (result.parity & 0x80) == 0)
        {
            _manager.parity_signal->wait_on_this_side(INFINITE);
            continue;
        }
        while (true)
        {
            auto old_value = result.value;

            auto new_result = result;
            new_result.sequence_number++;
            new_result.requested_client_count--;
            result.value = _InterlockedCompareExchange64(&result_reference.value, new_result.value, old_value);
            assert(result.frame_number == n);
            assert((result.parity & 0x80) != 0);
            if (result.value == old_value)
            {
                return !!(result.parity & 0x1);
            }
        }
    }
}

const VideoInfo& SharedMemoryClient::GetVideoInfo()
{
    return _vi;
}

int SharedMemoryClient::SetCacheHints(int cachehints,int frame_range)
{
    // we don't handle caching here
  return 0;
}

void SharedMemoryClient::GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env)
{
    // this shouldn't be called since we disabled audio
    assert(false);
}