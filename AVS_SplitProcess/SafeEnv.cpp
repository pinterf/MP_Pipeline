#include "common.h"
#include "SafeEnv.h"
#include <assert.h>
#include <mutex>

// serializes the frame allocations of the threads of this process
static std::mutex _lock;

PVideoFrame SafeNewVideoFrame(IScriptEnvironment* env, const VideoInfo& vi, int align)
{
    std::lock_guard<std::mutex> lock(_lock);
    PVideoFrame frame = env->NewVideoFrame(vi, align);
    assert(frame->IsWritable());
    return frame;
}

bool SafeMakeWritable(IScriptEnvironment* env, PVideoFrame* pvf)
{
    std::lock_guard<std::mutex> lock(_lock);
    bool result = env->MakeWritable(pvf);
    assert(pvf->operator->()->IsWritable());
    return result;
}
