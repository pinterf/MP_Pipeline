#include "common.h"
#include "TwoSidedLock.h"
#include "ActivitySignal.h"

#define LOCK_NAME_SUFFIX_SERVER SYSTEXT("_Server")
#define LOCK_NAME_SUFFIX_CLIENT SYSTEXT("_Client")

TwoSidedLock::TwoSidedLock(const SYSCHAR* lock_prefix, const SYSCHAR* lock_name, bool is_server_side, ActivitySignal* activity) :
    _is_server_side(is_server_side),
    _activity(activity)
{
    const sys_string name = sys_string(lock_prefix) + SYSTEXT("_") + lock_name;
    const sys_string server_name = name + LOCK_NAME_SUFFIX_SERVER;
    const sys_string client_name = name + LOCK_NAME_SUFFIX_CLIENT;
    if (is_server_side)
    {
        // the server's side starts signaled
        _event_this_side.reset(new platform::NamedEvent(server_name, true, true, true));
        _event_other_side.reset(new platform::NamedEvent(client_name, true, true, false));
    } else {
        _event_this_side.reset(new platform::NamedEvent(client_name, false, true, false));
        _event_other_side.reset(new platform::NamedEvent(server_name, false, true, false));
    }
}

void TwoSidedLock::signal_activity_if(bool server_side_event_set)
{
    if (server_side_event_set && _activity)
    {
        _activity->signal();
    }
}

void TwoSidedLock::switch_to_other_side()
{
    _event_other_side->set();
    signal_activity_if(!_is_server_side);
}

void TwoSidedLock::stay_on_this_side()
{
    _event_this_side->set();
    signal_activity_if(_is_server_side);
}

bool TwoSidedLock::wait_on_this_side(unsigned ms_timeout)
{
    if (!_event_this_side->wait(ms_timeout))
    {
        return false;
    }
    _event_this_side->reset();
    return true;
}

void TwoSidedLock::signal_all()
{
    _event_this_side->set();
    _event_other_side->set();
    signal_activity_if(true);
}

TwoSidedLockContext::TwoSidedLockContext(TwoSidedLock& lock) :
    _lock(lock)
{
}

TwoSidedLockContext::~TwoSidedLockContext()
{
    _lock.switch_to_other_side();
}

TwoSidedLockAcquire::TwoSidedLockAcquire(TwoSidedLock& lock) :
    TwoSidedLockContext(lock)
{
    _lock.wait_on_this_side(platform::WAIT_INFINITE);
}