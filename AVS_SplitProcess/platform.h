#pragma once

// The operating system services MP_Pipeline uses, behind a portable interface.
// Implementation: platform_win32.cpp (Windows XP and later). A POSIX implementation is planned,
// see LINUX_PORT_PLAN.md. No Windows header here: the rest of the code calls only these.
//
// Not here yet: the slave process (start, pipes, environment, lifetime), which is the next part.

#include <stdint.h>
#include <string>
#include <vector>

#include "NonCopyableClassBase.h"
#include "SystemChar.h"

namespace platform {

// timeout value: wait without a time limit
static const unsigned WAIT_INFINITE = 0xFFFFFFFF;

// --- processes and threads ---------------------------------------------------------------------

uint32_t current_process_id();
uint32_t current_thread_id();

// false when the process has exited, does not exist, or can't be queried
bool is_process_alive(uint32_t process_id);

// --- processors --------------------------------------------------------------------------------

// number of logical processors of the system (at least 1)
int processor_count();

// For "### lock threads to cores": the logical processors of each physical core as bit masks
// (bit n: logical processor n), and the processors the process may use. False on failure.
struct ProcessorTopology
{
    std::vector<uint64_t> core_masks;
    uint64_t usable_mask;
};
bool get_processor_topology(ProcessorTopology& topology);

// Locks the calling thread to one logical processor (cpu < 0: no change). Called by the thread
// itself, so it works with any std::thread implementation. Returns false on failure.
bool lock_current_thread_to_cpu(int cpu);

// --- misc --------------------------------------------------------------------------------------

// a new unique id (a UUID as text), for the names of the kernel objects of an instance;
// empty on failure
std::string new_unique_id();

// a line for the debugger / DebugView (OutputDebugString on Windows)
void debug_output(const char* text);

// --- named events (between processes) ----------------------------------------------------------

// Like a Win32 event object: manual reset (set() releases every waiter, stays signaled until
// reset()) or auto reset (one waiter, then reset). The server creates it, the clients open it
// by name.
class NamedEvent : private NonCopyableClassBase
{
public:
    // create: a new event, throws std::runtime_error when it can't be created or already exists;
    // otherwise opens an existing one, throws when it does not exist
    NamedEvent(const sys_string& name, bool create, bool manual_reset, bool initially_set);
    ~NamedEvent();

    void set();
    void reset();
    // true: signaled, false: timeout; throws std::runtime_error on failure
    bool wait(unsigned ms_timeout = WAIT_INFINITE);

private:
    void* _handle;
};

// --- named shared memory -----------------------------------------------------------------------

class SharedMapping : private NonCopyableClassBase
{
public:
    enum Result
    {
        OK,
        ALREADY_EXISTS, // create: the name is in use
        NOT_FOUND,      // open: no mapping with the name
        FAILED,         // could not create (e.g. out of memory)
        MAP_FAILED      // created / opened, but not mapped (e.g. out of address space)
    };

    SharedMapping();
    ~SharedMapping();

    // a new mapping of size bytes, zero filled, mapped writable
    Result create(const sys_string& name, uint64_t size);
    // an existing mapping, mapped completely
    Result open(const sys_string& name, bool writable);
    // unmaps; the mapping itself lives on while another process has it mapped
    void close();

    uint8_t* data() const { return _view; }
    bool is_open() const { return _view != nullptr; }

private:
    void* _handle;
    uint8_t* _view;
};

} // namespace platform
