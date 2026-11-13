#pragma once

// Wakes the server thread when anything it handles may have happened: a client request, a
// client releasing a response slot, a frame arriving in the fetcher cache, shutdown.
// Replaces the server's wait on many events (WaitForMultipleObjects).
//
// A counter in the shared memory plus one named auto-reset event:
// - signal(): increments the counter, then sets the event (any process, any thread);
// - wait() (the server thread only): returns at once when the counter changed since the
//   previous wait, otherwise waits for the event. A signal between reading the counter and
//   waiting leaves the event set, so no wakeup is lost; spurious wakeups are possible, the
//   server re-checks its state anyway.
// The counter is also what a futex based implementation (Linux) would wait on.

#include <atomic>
#include <stdint.h>

#include "Handle.h"
#include "NonCopyableClassBase.h"
#include "SystemChar.h"

class ActivitySignal : private NonCopyableClassBase
{
public:
    // the server creates the event, the clients open it
    ActivitySignal(const sys_string& name, std::atomic<uint32_t>& counter, bool is_server);

    void signal();
    void wait();

private:
    std::atomic<uint32_t>& _counter;
    uint32_t _last_seen;
    OwnedEventHandle _event;
};
