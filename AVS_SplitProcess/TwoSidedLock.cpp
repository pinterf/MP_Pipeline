#include "common.h"
#include "TwoSidedLock.h"
#include "ActivitySignal.h"

#include <algorithm>
#include <chrono>

#define LOCK_NAME_SUFFIX_SERVER SYSTEXT("_Server")
#define LOCK_NAME_SUFFIX_CLIENT SYSTEXT("_Client")

static int64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

TwoSidedLock::TwoSidedLock(const SYSCHAR* lock_prefix, const SYSCHAR* lock_name, bool is_server_side, ActivitySignal* activity) :
    _is_server_side(is_server_side),
    _activity(activity),
    _peer_process_id(0),
    _next_peer_check_ms(0)
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
    if (!_peer_process_id)
    {
        if (!_event_this_side->wait(ms_timeout))
        {
            return false;
        }
        _event_this_side->reset();
        return true;
    }
    // in slices: the peer is checked between them
    const int64_t deadline = ms_timeout == platform::WAIT_INFINITE ? 0 : now_ms() + ms_timeout;
    while (true)
    {
        unsigned slice = PEER_CHECK_MS;
        if (ms_timeout != platform::WAIT_INFINITE)
        {
            slice = (unsigned)std::max<int64_t>(0, std::min<int64_t>(slice, deadline - now_ms()));
        }
        if (_event_this_side->wait(slice))
        {
            _event_this_side->reset();
            return true;
        }
        check_peer();
        if (ms_timeout != platform::WAIT_INFINITE && now_ms() >= deadline)
        {
            return false;
        }
    }
}

void TwoSidedLock::watch_peer(uint32_t process_id)
{
    _peer_process_id = process_id;
    _next_peer_check_ms = now_ms() + PEER_CHECK_MS;
}

void TwoSidedLock::check_peer()
{
    const int64_t now = now_ms();
    int64_t next = _next_peer_check_ms;
    if (now < next || !_next_peer_check_ms.compare_exchange_strong(next, now + PEER_CHECK_MS))
    {
        return; // not yet, or another thread checks now
    }
    if (!platform::is_process_alive(_peer_process_id))
    {
        // stays due: every later wait throws too
        _next_peer_check_ms = 0;
        throw PeerLostError();
    }
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