#include "common.h"
#include "ActivitySignal.h"

ActivitySignal::ActivitySignal(const sys_string& name, std::atomic<uint32_t>& counter, bool is_server) :
    _counter(counter),
    _last_seen(counter.load()),
    // auto reset: one signal wakes the server thread once
    _event(name, is_server, false, false)
{
}

void ActivitySignal::signal()
{
    _counter.fetch_add(1);
    _event.set();
}

void ActivitySignal::wait()
{
    uint32_t now = _counter.load();
    if (now == _last_seen)
    {
        _event.wait(platform::WAIT_INFINITE);
        now = _counter.load();
    }
    _last_seen = now;
}