#define UNICODE
#define _UNICODE

#include "common.h"
#include "CondVar.h"
#include "utils.h"
#include <assert.h>

CondVar::CondVar(spin_lock_t* lock_ptr, const sys_string& event_name, bool is_server) :
    lock(lock_ptr),
    signal(event_name.c_str(), SYSTEXT("Cond"), is_server)
{
}

void CondVar::lock_short()
{
    static const int SPIN_COUNT = SPIN_LOCK_UNIT * 5 < 2000 ? 2000 : SPIN_LOCK_UNIT * 5;
#if _DEBUG
    __int64 enter_time = us_time();
#endif
    while (true)
    {
        if (lock.try_lock(SPIN_COUNT))
        {
#if _DEBUG
            if (us_time() - enter_time > 100 * 1000) // 100ms
            {
                assert(("The other side has taken the lock for a long time!", false));
            }
#endif
            return;
        }
        signal.wait_on_this_side(1);
    }
}

bool CondVar::lock_long(unsigned timeout)
{
    while (true)
    {
        if (lock.try_lock(SPIN_LOCK_UNIT))
        {
            break;
        }
        if (lock.try_sleep_lock(SLEEP_LOCK_UNIT))
        {
            break;
        }
        if (!signal.wait_on_this_side(timeout, TRUE))
        {
            return false;
        }
    }
    return true;
}

CondVarLockAcquire::CondVarLockAcquire(CondVar& cond, bool lock_type) :
    _cond(cond),
    signal_after_unlock(false)
{
    if (lock_type == LOCK_SHORT)
    {
        cond.lock_short();
    } else {
        cond.lock_long();
    }
}
CondVarLockAcquire::~CondVarLockAcquire()
{
    _cond.lock.unlock();
    if (signal_after_unlock)
    {
        _cond.signal.switch_to_other_side();
    }
}