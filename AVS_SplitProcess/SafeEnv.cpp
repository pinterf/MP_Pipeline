#include "common.h"
#include "SafeEnv.h"
#include "Lock.h"
#include <assert.h>

enum {
    UNINITIALIZED = 0,
    INITIALIZING,
    INITIALIZED
};

volatile LONG _initialization_state = UNINITIALIZED;

CriticalSectionLock* _lock = NULL;

void ensure_initialized()
{
    if (_initialization_state != INITIALIZED)
    {
        while (true)
        {
            LONG original_state = InterlockedCompareExchange(&_initialization_state, INITIALIZING, UNINITIALIZED);
            switch (original_state)
            {
            case UNINITIALIZED:
                *((volatile CriticalSectionLock**)&_lock) = new CriticalSectionLock();
                InterlockedExchange(&_initialization_state, INITIALIZED);
                break;
            case INITIALIZING:
                Sleep(0);
                continue;
            // default:
                // fall below
            }
            break;
        }
    }
    assert(_lock);
}

PVideoFrame SafeNewVideoFrame(IScriptEnvironment* env, const VideoInfo& vi, int align)
{
    ensure_initialized();
    {
        CSLockAcquire lock(*_lock);
        PVideoFrame frame = env->NewVideoFrame(vi, align);
        assert(frame->IsWritable());
        return frame;
    }
}

bool SafeMakeWritable(IScriptEnvironment* env, PVideoFrame* pvf)
{
    ensure_initialized();
    {
        CSLockAcquire lock(*_lock);
        bool result = env->MakeWritable(pvf);
        assert(pvf->operator->()->IsWritable());
        return result;
    }
}