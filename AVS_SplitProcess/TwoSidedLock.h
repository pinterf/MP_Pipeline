#pragma once

#include <memory>

#include "platform.h"
#include "SystemChar.h"
#include "NonCopyableClassBase.h"

class ActivitySignal;

// Two named manual-reset events between the server and a client process: one for each side.
// The server creates them ("<prefix>_<name>_Server", initially set, and "..._Client"), the client
// opens them. A side waits on its own event and sets the other one to hand over.
class TwoSidedLock : private NonCopyableClassBase
{
public:
    // activity: signaled whenever the server side's event is set (by either side), so the
    // server thread wakes up; NULL for locks the server thread does not wait for
    TwoSidedLock(const SYSCHAR* lock_prefix, const SYSCHAR* lock_name, bool is_server_side, ActivitySignal* activity = NULL);

    void switch_to_other_side();
    void stay_on_this_side();
    // true: signaled (and reset), false: timeout
    bool wait_on_this_side(unsigned ms_timeout);
    void signal_all();

private:
    std::unique_ptr<platform::NamedEvent> _event_this_side, _event_other_side;
    const bool _is_server_side;
    ActivitySignal* const _activity;

    void signal_activity_if(bool server_side_event_set);
};

class TwoSidedLockContext : private NonCopyableClassBase
{
public:
    TwoSidedLockContext(TwoSidedLock& lock);
    ~TwoSidedLockContext();
protected:
    TwoSidedLock& _lock;
};

class TwoSidedLockAcquire : private TwoSidedLockContext
{
public:
    TwoSidedLockAcquire(TwoSidedLock& lock);
};