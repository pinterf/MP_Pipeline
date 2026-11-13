#pragma once

#include <Windows.h>
#include "Handle.h"
#include "SystemChar.h"
#include "NonCopyableClassBase.h"

class ActivitySignal;

class TwoSidedLock : private NonCopyableClassBase
{
public:
    // activity: signaled whenever the server side's event is set (by either side), so the
    // server thread wakes up; NULL for locks the server thread does not wait for
    TwoSidedLock(const SYSCHAR* lock_prefix, const SYSCHAR* lock_name, bool is_server_side, ActivitySignal* activity = NULL);

    void switch_to_other_side();
    void stay_on_this_side();
    bool wait_on_this_side(DWORD ms_timeout, BOOL apc_aware = FALSE);
    void signal_all();

private:
    OwnedEventHandle _event_this_side, _event_other_side;
    const bool _is_server_side;
    ActivitySignal* const _activity;

    void signal_activity_if(bool server_side_event_set);

    TwoSidedLock(const TwoSidedLock&);
    
    void create_server(const SYSCHAR* lock_name);
    void create_client(const SYSCHAR* lock_name);
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