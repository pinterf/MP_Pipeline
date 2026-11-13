#pragma once

// An event between the threads of one process (std::mutex + std::condition_variable), with the
// semantics of a Win32 event object:
// - manual reset: set() releases every waiter and stays signaled until reset();
// - auto reset: set() releases one waiter, the successful wait resets it; a set() without a
//   waiter stays signaled until the next wait.
// Not for other processes: those use the named events (TwoSidedLock).

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "NonCopyableClassBase.h"

class LocalEvent : private NonCopyableClassBase
{
public:
    static const unsigned WAIT_INFINITE = 0xFFFFFFFF;

    explicit LocalEvent(bool manual_reset, bool initially_signaled = false) :
        _manual_reset(manual_reset), _signaled(initially_signaled) {}

    void set()
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _signaled = true;
        }
        if (_manual_reset)
        {
            _cond.notify_all();
        } else {
            _cond.notify_one();
        }
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _signaled = false;
    }

    // true: signaled, false: timeout
    bool wait(unsigned ms_timeout = WAIT_INFINITE)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        return wait_locked(lock, ms_timeout);
    }

    // waits, then resets (in one step, also for a manual reset event)
    bool wait_and_reset(unsigned ms_timeout = WAIT_INFINITE)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        const bool signaled = wait_locked(lock, ms_timeout);
        _signaled = false;
        return signaled;
    }

private:
    bool wait_locked(std::unique_lock<std::mutex>& lock, unsigned ms_timeout)
    {
        auto is_signaled = [this] { return _signaled; };
        if (ms_timeout == WAIT_INFINITE)
        {
            _cond.wait(lock, is_signaled);
        } else if (!_cond.wait_for(lock, std::chrono::milliseconds(ms_timeout), is_signaled)) {
            return false;
        }
        if (!_manual_reset)
        {
            _signaled = false;
        }
        return true;
    }

    const bool _manual_reset;
    bool _signaled;
    std::mutex _mutex;
    std::condition_variable _cond;
};
