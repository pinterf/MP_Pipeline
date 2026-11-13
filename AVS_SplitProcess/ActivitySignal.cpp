#define UNICODE
#define _UNICODE

#include "common.h"
#include "ActivitySignal.h"

#include <sstream>
#include <stdexcept>

ActivitySignal::ActivitySignal(const sys_string& name, std::atomic<uint32_t>& counter, bool is_server) :
    _counter(counter),
    _last_seen(counter.load()),
    _event(NULL)
{
    SetLastError(ERROR_SUCCESS);
    if (is_server)
    {
        _event.replace(CreateEvent(NULL, FALSE, FALSE, name.c_str()));
        if (_event.is_valid() && GetLastError() == ERROR_ALREADY_EXISTS)
        {
            throw std::runtime_error("The activity event already exists.");
        }
    } else {
        _event.replace(OpenEvent(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, name.c_str()));
    }
    if (!_event.is_valid())
    {
        throw std::runtime_error("Unable to create or open the activity event.");
    }
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
        DWORD result = _event.wait(INFINITE);
        if (result != WAIT_OBJECT_0)
        {
            std::ostringstream ss;
            ss << "Unexpected error: wait failed, result: " << result << ", code: " << GetLastError();
            throw std::runtime_error(ss.str());
        }
        now = _counter.load();
    }
    _last_seen = now;
}
