#pragma once

#include "avisynth.h"
#include <atomic>
#include <mutex>
#include "LocalEvent.h"

#include <deque>
#include <vector>
#include <string>
#include <functional>
#include <thread>

class ClipInfo
{
public:
    ClipInfo(PClip clip) : 
      clip(clip), 
      frame_cache(), 
      cache_frame_start(0), 
      last_requested_frame(0),
      error_msg()
    {
        vi = clip->GetVideoInfo();
    };

    PClip clip;
    std::deque<PVideoFrame> frame_cache;
    int cache_frame_start;
    int last_requested_frame;
    std::string error_msg;

    // we assume that vi never changes to improve performance
    // actually other filters may have bigger problem if vi changes
    VideoInfo vi;
};

typedef struct _FetchInfo
{
    int clip_index;
    int frame_number;
    bool is_fetching;
} FetchInfo;

class FrameFetcher
{
public:
    FrameFetcher(const PClip clips[], int max_cache_frames, int cache_behind, IScriptEnvironment* env);
    ~FrameFetcher();
    
    PVideoFrame GetFrame(int clip_index, int n, IScriptEnvironment* env);
    bool GetParity(int clip_index, int n);
    void GetAudio(int clip_index, void* buf, __int64 start, __int64 count, IScriptEnvironment* env);
    const VideoInfo& GetVideoInfo(int clip_index);

    PVideoFrame try_get_frame_from_cache(int clip_index, int n);
    void set_last_requested_frame(int clip_index, int n, bool set_only_if_larger);

    bool is_valid_clip_index(int index);
    void signal_shutdown();


    // called after a frame was added to the cache, while holding the fetcher's lock (keep it
    // short, don't call the fetcher from it); nullptr: no more calls after this returns
    void set_new_frame_callback(std::function<void (void)> callback);
protected:
    std::mutex _lock;

private:
    unsigned thread_proc();

    void invoke_in_worker_thread(std::function<void (void)> func);
    void work_item_completed(unsigned wait_time = LocalEvent::WAIT_INFINITE);
    void wait_for_work_item_complete();
    
    void fetch_frame(ClipInfo& clip, int n);
    PVideoFrame try_get_frame(ClipInfo& clip, int n);

    std::atomic<bool> _shutdown;
    int _max_cache_frames;
    int _cache_behind;
    std::vector<ClipInfo> _clips;

    IScriptEnvironment* _env;

    FetchInfo _fetch_info; // protected by _lock
    std::function<void (void)> _worker_callback;
    std::function<void (void)> _new_frame_callback; // protected by _lock

    std::thread _worker_thread;
    // manual reset: every thread waiting for a work item is released when it is signaled
    LocalEvent _worker_workitem_completed_event;
    // auto reset: wakes the worker
    LocalEvent _worker_waiting_for_work_event;
};