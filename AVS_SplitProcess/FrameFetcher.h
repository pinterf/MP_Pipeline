#pragma once

#include "avisynth.h"
#include <atomic>
#include <mutex>
#include "Handle.h"
#include "LocalEvent.h"

#include <Windows.h>
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
    // worker_cpu: the logical processor the worker thread is locked to, -1: none
    FrameFetcher(const PClip clips[], int max_cache_frames, int cache_behind, IScriptEnvironment* env, int worker_cpu = -1);
    ~FrameFetcher();
    
    PVideoFrame GetFrame(int clip_index, int n, IScriptEnvironment* env);
    bool GetParity(int clip_index, int n);
    void GetAudio(int clip_index, void* buf, __int64 start, __int64 count, IScriptEnvironment* env);
    const VideoInfo& GetVideoInfo(int clip_index);

    PVideoFrame try_get_frame_from_cache(int clip_index, int n);
    void set_last_requested_frame(int clip_index, int n, bool set_only_if_larger);

    bool is_valid_clip_index(int index);
    void signal_shutdown();


    OwnedEventHandle new_frame_in_cache_event;
protected:
    std::mutex _lock;

private:
    unsigned thread_proc();

    void invoke_in_worker_thread(std::function<void (void)> func);
    void work_item_completed(DWORD wait_time = INFINITE);
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

    int _worker_cpu;
    std::thread _worker_thread;
    // manual reset: every thread waiting for a work item is released when it is signaled
    LocalEvent _worker_workitem_completed_event;
    // auto reset: wakes the worker
    LocalEvent _worker_waiting_for_work_event;
};