#pragma once

#include <atomic>
#include <memory>
#include <stdexcept>

#include "platform.h"
#include "SystemChar.h"
#include "NonCopyableClassBase.h"

class ActivitySignal;

// thrown by a wait of a watched lock when the process of the other side is gone (killed, crashed):
// its events would never be set again
class PeerLostError : public std::runtime_error
{
public:
    PeerLostError() : std::runtime_error("The upstream slave process ended unexpectedly.") {}
};

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
    // true: signaled (and reset), false: timeout; throws PeerLostError when watching a process
    // that is gone
    bool wait_on_this_side(unsigned ms_timeout);
    void signal_all();

    // the client side watches the server's process: while waiting, it is checked about every
    // PEER_CHECK_MS (also over many short waits)
    void watch_peer(uint32_t process_id);
    static const unsigned PEER_CHECK_MS = 250;

private:
    std::unique_ptr<platform::NamedEvent> _event_this_side, _event_other_side;
    const bool _is_server_side;
    ActivitySignal* const _activity;
    uint32_t _peer_process_id;
    std::atomic<int64_t> _next_peer_check_ms;

    void signal_activity_if(bool server_side_event_set);
    void check_peer();
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