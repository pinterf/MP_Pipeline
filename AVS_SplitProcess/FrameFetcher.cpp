#include "common.h"
#include "FrameFetcher.h"

// #define ENABLE_TRACING
#define TRACE_PREFIX __TRACE_TEXT("FrameFetcher")
#include "trace.h"
#include "utils.h"

#include <process.h>
#include <assert.h>


using namespace std;

void wait_and_reset_event(OwnedEventHandle& event)
{
    event.wait();
    event.reset();
}

unsigned __stdcall FrameFetcher::thread_stub(void* fetcher)
{
    return ((FrameFetcher*)fetcher)->thread_proc();
}

unsigned FrameFetcher::thread_proc()
{
    try
    {
        while (!_shutdown)
        {
            ClipInfo* clip_to_fetch = NULL;
            int requested_frame = -1;
            function<void (void)> callback(nullptr);

            bool has_no_work = false;

            { // lock start
                CSLockAcquire lock(_lock);
                if (_worker_callback) {
                    callback = _worker_callback;
                } else if (_fetch_info.is_fetching) {
                    clip_to_fetch = &_clips[_fetch_info.clip_index];
                    requested_frame = _fetch_info.frame_number;
                    if (!clip_to_fetch->error_msg.empty())
                    {
                        _fetch_info.is_fetching = false;
                        continue;
                    }
                    if (clip_to_fetch->cache_frame_start <= requested_frame && 
                        clip_to_fetch->cache_frame_start + (int)clip_to_fetch->frame_cache.size() > requested_frame)
                    {
                        // let the waiting thread get its frame first
                        clip_to_fetch = NULL;
                        requested_frame = -1;
                    }
                } else {
                    int max_cache_space = 0;
                    int prefetch_clip_index = -1;
                    for (size_t i = 0; i < (int)_clips.size(); i++)
                    {
                        ClipInfo& clip = _clips[i];
                        int next_uncached_frame = clip.cache_frame_start + (int)clip.frame_cache.size();
                        if (next_uncached_frame >= clip.vi.num_frames)
                        {
                            continue;
                        }
                        int cache_space = _max_cache_frames - (int)clip.frame_cache.size();
                        cache_space += max(0, (clip.last_requested_frame - clip.cache_frame_start) - _cache_behind);
                        if (cache_space <= 0) 
                        {
                            continue;
                        }
                        if (!clip.error_msg.empty())
                        {
                            continue;
                        }
                        if (cache_space > max_cache_space)
                        {
                            clip_to_fetch = &clip;
                            requested_frame = next_uncached_frame;
                            max_cache_space = cache_space;
                            prefetch_clip_index = (int)i;
                            assert(requested_frame >= 0);
                        }
                    }
                    if (!clip_to_fetch)
                    {
                        TRACE("Cache is full");
                        has_no_work = true;
                    } else {
                        TRACE("Prefetching clip %d, frame %d", prefetch_clip_index, requested_frame);
                    }
                }
            } // lock end

            if (callback)
            {
                callback();
                { // lock start
                    CSLockAcquire lock(_lock);
                    _worker_callback = nullptr;
                } // lock end
                work_item_completed(1);
                continue;
            }

            if (!clip_to_fetch)
            {
                work_item_completed(has_no_work ? INFINITE : 10);
                continue;
            }

            assert(requested_frame >= 0);

            fetch_frame(*clip_to_fetch, requested_frame);

            work_item_completed(1);
        }
    } catch (...) {
        __debugbreak();
        throw;
    }
    return 0;
}

void FrameFetcher::work_item_completed(DWORD wait_time)
{
#ifdef _DEBUG
    assert(!_lock.IsOwnedLock());
#endif
    _worker_workitem_completed_event.set();
    _worker_waiting_for_work_event.wait(wait_time);
}

void FrameFetcher::fetch_frame(ClipInfo& clip, int n)
{
    int fetch_start = -1;

    { // lock start
        CSLockAcquire lock(_lock);
        if (n >= clip.cache_frame_start)
        {
            while (clip.frame_cache.size() > 0 && n - clip.cache_frame_start + 1 > _max_cache_frames)
            {
                TRACE("Removing frame %d from cache", clip.cache_frame_start);
                clip.frame_cache.pop_front();
                clip.cache_frame_start++;
            }
        } else {
            clip.frame_cache.clear();
        }
        if (clip.frame_cache.size() == 0) 
        {
            TRACE("Cache is empty now");
            clip.cache_frame_start = n;
            fetch_start = n;
        } else {
            fetch_start = clip.cache_frame_start + (int)clip.frame_cache.size();
        }
    } // lock end
    
    // sanity check
    assert(fetch_start >= 0);
    assert(n - fetch_start <= _cache_behind);

    while (fetch_start <= n)
    {
#ifdef _DEBUG
        __int64 start_time = us_time();
#endif
        PVideoFrame frame = try_get_frame(clip, fetch_start);
        if (!frame)
        {
            return;
        }
#ifdef _DEBUG
        TRACE("Time elapsed while fetching frame %d: %lld us", n, us_time() - start_time);
#endif
        { // lock start
            CSLockAcquire lock(_lock);
            TRACE("Adding frame %d to cache", clip.cache_frame_start + clip.frame_cache.size());
            clip.frame_cache.push_back(frame);
        } // lock end
        fetch_start++;
        new_frame_in_cache_event.pulse();
    }
}

PVideoFrame FrameFetcher::try_get_frame_from_cache(int clip_index, int n)
{
    CSLockAcquire lock(_lock);
    ClipInfo& info = _clips[clip_index];
    int index = n - info.cache_frame_start;
    if (index >= 0 && index < (int)info.frame_cache.size())
    {
        return info.frame_cache[index];
    } else {
        return NULL;
    }
}

void FrameFetcher::set_last_requested_frame(int clip_index, int n, bool set_only_if_larger)
{
    {
        CSLockAcquire lock(_lock);
        ClipInfo& info = _clips[clip_index];
        if (set_only_if_larger && n <= info.last_requested_frame)
        {
            return;
        }
        info.last_requested_frame = n;
    }
    _worker_waiting_for_work_event.set();
}

PVideoFrame FrameFetcher::try_get_frame(ClipInfo& clip, int n)
{
    assert(clip.error_msg.empty());
    assert(n >= 0 && n < clip.vi.num_frames);
    PVideoFrame frame = NULL;
    
    try
    {
        frame = clip.clip->GetFrame(n, _env);
    } catch (AvisynthError& ex) {
        CSLockAcquire lock(_lock);
        clip.error_msg.assign(ex.msg);
        clip.frame_cache.clear();
        frame = NULL;
    }
    return frame;
}

void FrameFetcher::invoke_in_worker_thread(function<void (void)> func)
{
    bool completed = false;
    bool stub_set = false;
    auto function_stub = [&func, &completed] {
        func();
        completed = true;
    };
    while (!completed)
    {
        { // lock start
            CSLockAcquire lock(_lock);
            if (!_worker_callback)
            {
                if (completed) 
                {
                    break;
                }
                assert(!stub_set);
                _worker_callback = function_stub;
                stub_set = true;
                _worker_waiting_for_work_event.set();
            }
        } // lock end
        wait_for_work_item_complete();
    }
}

void FrameFetcher::wait_for_work_item_complete()
{
    wait_and_reset_event(_worker_workitem_completed_event);
}

bool FrameFetcher::is_valid_clip_index(int index)
{
    return index >= 0 && index < (int)_clips.size();
}

bool FrameFetcher::GetParity(int clip_index, int n)
{
    assert(clip_index >= 0 && clip_index < (int)_clips.size());
    ClipInfo& clip = _clips[clip_index];
    bool result;
    invoke_in_worker_thread([&clip, &result, n] {
        result = clip.clip->GetParity(n);
    });
    return result;
}

void FrameFetcher::GetAudio(int clip_index, void* buf, __int64 start, __int64 count, IScriptEnvironment* env)
{
    assert(env == _env);
    assert(clip_index >= 0 && clip_index < (int)_clips.size());
    ClipInfo& clip = _clips[clip_index];
    string error_msg;
    invoke_in_worker_thread([&clip, buf, start, count, env, &error_msg] {
        try
        {
            clip.clip->GetAudio(buf, start, count, env);
        } catch (AvisynthError& e) {
            error_msg.assign(e.msg);
        }
    });
    if (!error_msg.empty())
    {
        env->ThrowError(error_msg.c_str());
    }
}

const VideoInfo& FrameFetcher::GetVideoInfo(int clip_index)
{
    assert(clip_index >= 0 && clip_index < (int)_clips.size());
    ClipInfo& clip = _clips[clip_index];
    return clip.vi;
}

PVideoFrame FrameFetcher::GetFrame(int clip_index, int n, IScriptEnvironment* env)
{
    assert(env == _env);
    assert(clip_index >= 0 && clip_index < (int)_clips.size());
    bool already_set_fetching_flag = false;
    while (true)
    {
        { // lock start
            CSLockAcquire lock(_lock);
            if (_shutdown)
            {
                env->ThrowError("FrameFetcher: Already shut down!");
            }
            ClipInfo& clip = _clips[clip_index];
            if (!clip.error_msg.empty())
            {
                env->ThrowError(clip.error_msg.c_str());
            }
            if (n >= clip.cache_frame_start && n < clip.cache_frame_start + (int)clip.frame_cache.size())
            {
                clip.last_requested_frame = n;
                if (already_set_fetching_flag)
                {
                    _fetch_info.is_fetching = false;
                }
                return clip.frame_cache.at(n - clip.cache_frame_start);
            }
            if (!_fetch_info.is_fetching)
            {
                assert(!already_set_fetching_flag);
                _fetch_info.is_fetching = true;
                _fetch_info.clip_index = clip_index;
                _fetch_info.frame_number = n;
                already_set_fetching_flag = true;
                _worker_waiting_for_work_event.set();
            }
        } // lock end
        wait_for_work_item_complete();
    }
}

bool FrameFetcher::set_worker_thread_affinity(DWORD_PTR affinity_mask)
{
    assert(!_shutdown);
    assert(_worker_thread.is_valid());
    return !!SetThreadAffinityMask(_worker_thread.get(), affinity_mask);
}

FrameFetcher::FrameFetcher(const PClip clips[], int max_cache_frames, int cache_behind, IScriptEnvironment* env)
    : _max_cache_frames(max_cache_frames <= cache_behind ? cache_behind + 1 : max_cache_frames),  
      _cache_behind(cache_behind),
      _env(env),
      _clips(),
      _lock(),
      _worker_callback(),
      _worker_thread(NULL),
      _worker_workitem_completed_event(CreateEvent(NULL, TRUE, FALSE, NULL)), // needs to be manual reset, since we want all threads waiting on this event to be released when it is signaled
      _worker_waiting_for_work_event(CreateEvent(NULL, FALSE, FALSE, NULL)),
      new_frame_in_cache_event(CreateEvent(NULL, TRUE, FALSE, NULL)),
      _shutdown(false)
{
    if (_max_cache_frames <= 0)
    {
        env->ThrowError("FrameFetcher: max_cache_frames must be greater than 0.");
    }

    if (_cache_behind < 0)
    {
        env->ThrowError("FrameFetcher: cache_behind must be greater than or equal to 0.");
    }

    memset((void*)&_fetch_info, 0, sizeof(_fetch_info));

    if (!_worker_workitem_completed_event.is_valid() || !_worker_waiting_for_work_event.is_valid())
    {
        env->ThrowError("FrameFetcher: Unable to create event.");
    }

    int i = 0;
    while (clips[i])
    {
        _clips.push_back(ClipInfo(clips[i]));
        i++;
    }
    if (_clips.size() == 0)
    {
        env->ThrowError("FrameFetcher: Unexpected exception: No input clip.");
    }
    _worker_thread.replace((HANDLE)_beginthreadex(NULL, 0, FrameFetcher::thread_stub, this, 0, NULL));
    if (!_worker_thread.is_valid())
    {
        env->ThrowError("FrameFetcher: Can't start worker thread.");
    }
}

void FrameFetcher::signal_shutdown()
{
    _shutdown = true;
    _worker_waiting_for_work_event.set();
    _worker_workitem_completed_event.set();
}

FrameFetcher::~FrameFetcher()
{
    signal_shutdown();
    _worker_thread.wait();
    _env = NULL;
}