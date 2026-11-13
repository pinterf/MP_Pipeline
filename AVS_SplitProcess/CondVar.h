#pragma once

#include "SpinLock.h"
#include "TwoSidedLock.h"
#include "SystemChar.h"
#include "NonCopyableClassBase.h"


class CondVar : NonCopyableClassBase
{
public:
    CondVar(spin_lock_t* lock_ptr, const sys_string& event_name, bool is_server, ActivitySignal* activity = NULL);
    void lock_short(); // expect the lock won't be held for a long time
    bool lock_long(unsigned timeout = platform::WAIT_INFINITE); // the lock may be held for a long time
    
    SpinLock<> lock;
    TwoSidedLock signal;
};

class CondVarLockAcquire : NonCopyableClassBase
{
public:
    static const bool LOCK_SHORT = true;
    static const bool LOCK_LONG = false;

    bool signal_after_unlock;

    CondVarLockAcquire(CondVar& cond, bool lock_type);
    ~CondVarLockAcquire();
private:
    CondVar& _cond;
};
