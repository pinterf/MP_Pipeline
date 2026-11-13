#pragma once

// The operating system services MP_Pipeline uses, behind a portable interface.
// Implementations: platform_win32.cpp + platform_win32_slave.cpp (Windows XP and later),
// platform_posix.cpp + platform_posix_slave.cpp (Linux). No system header here: the rest of the
// code calls only these. See LINUX_PORT_PLAN.md.


#include <stdint.h>
#include <stdio.h>
#include <memory>
#include <string>

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


// --- misc --------------------------------------------------------------------------------------

// a new unique id (a UUID as text), for the names of the kernel objects of an instance;
// empty on failure
std::string new_unique_id();

// a line for the debugger / DebugView (OutputDebugString on Windows)
void debug_output(const char* text);

// --- text --------------------------------------------------------------------------------------

// "ANSI" is the code page AviSynth uses for script text and file names on Windows
// (AreFileApisANSI() ? CP_ACP : CP_OEMCP); characters it does not have become '?'.
// Elsewhere everything is UTF-8 and the ANSI conversions return their input.
// sys_string (SystemChar.h) is UTF-16 on Windows and UTF-8 elsewhere.
std::string ansi_to_utf8(const std::string& s);
std::string utf8_to_ansi(const std::string& s);
sys_string utf8_to_sys(const std::string& s);
std::string sys_to_utf8(const sys_string& s);
std::string sys_to_ansi(const sys_string& s);

// --- files, modules, environment ---------------------------------------------------------------

// full path of the module (the plugin DLL or the executable) containing this code; empty on failure
sys_string module_path();

bool file_exists(const sys_string& path);

// Reads a variable of this process' environment; false when it is not set. The narrow version
// uses the process' ANSI code page (Windows), the wide one is exact.
bool get_environment_variable(const char* name, std::string& value);
#ifdef _WIN32
bool get_environment_variable(const SYSCHAR* name, sys_string& value);
#endif

// Sets a variable in the environment of this process (value NULL: removes it); the child
// processes started afterwards inherit it. The narrow version uses the process' ANSI code page
// (Windows), the wide one is exact.
bool set_environment_variable(const char* name, const char* value);
#ifdef _WIN32
// elsewhere SYSCHAR is char: the narrow version above
bool set_environment_variable(const SYSCHAR* name, const SYSCHAR* value);
#endif

// the end of the slave executable's file name, after the plugin's: ".slave.exe" / ".slave"
const char* slave_executable_suffix();

// the AviSynth library a slave loads when its block has no "### dll:" statement
const char* default_avisynth_library();

// "### platform:" (a slave of the other bitness) is available: Windows only
bool supports_other_platforms();

// --- child processes (the slaves) --------------------------------------------------------------

// The slave processes of one MP_Pipeline instance. When the master process ends unexpectedly
// (crash, killed), the processes of the group are killed (Windows: a job object with
// kill-on-close). At a normal end they may finish on their own: set_kill_on_destroy(false).
class ChildProcessGroup : private NonCopyableClassBase
{
public:
    // throws std::runtime_error
    ChildProcessGroup();
    // kills the processes of the group, unless set_kill_on_destroy(false) was called
    ~ChildProcessGroup();

    void set_kill_on_destroy(bool kill);

private:
    friend class ChildProcess;
    void* _job;
    bool _kill_on_destroy;
};

// The write end of a child's stdin. The slave runs while it is open; closing it (destroying
// this object) tells the slave to exit.
class ChildInput : private NonCopyableClassBase
{
public:
    ~ChildInput();

private:
    friend class ChildProcess;
    explicit ChildInput(void* handle) : _handle(handle) {}
    void* _handle;
};

// A child process started with its stdin from a pipe and its stdout + stderr into another pipe.
// Destroying it closes the process handle and the pipes that were not handed over: the child
// sees the end of its stdin.
class ChildProcess : private NonCopyableClassBase
{
public:
    ChildProcess();
    ~ChildProcess();

    // Starts the executable in the group (if the system does not allow that, outside it: then
    // it is not killed with the master). The child inherits this process' environment.
    // false: error_message tells why.
    bool start(const sys_string& executable, ChildProcessGroup& group, std::string& error_message);

    // reads the child's output (stdout + stderr); 0: the output was closed (the child exited)
    size_t read_output(char* buffer, size_t size);

    // true and the exit code when the child exits within the timeout
    bool wait_for_exit(unsigned ms_timeout, uint32_t& exit_code);

    // hands over the stdin pipe, see ChildInput; null when already handed over
    std::unique_ptr<ChildInput> take_input();

private:
    void* _process;
    void* _input_write;
    void* _output_read;
};

// --- the slave program (platform_win32_slave.cpp, linked into the slave only) ------------------

// Keeps the handshake output apart from what filters print: returns a stream to the process'
// original stdout (the pipe the master reads) and points stdout to stderr. NULL on failure.
FILE* take_over_stdout();

// false and the reason when the process has no usable stdin (the master's pipe)
bool check_input(std::string& error_message);

// Reads stdin until the master closes it: the slave's work is done. false: a read error, with
// the reason.
bool wait_for_end_of_input(std::string& error_message);

// Loads a library (the AviSynth DLL) by name or path; it is never unloaded. NULL and the reason
// on failure.
void* load_library(const char* name, std::string& error_message);
void* library_symbol(void* library, const char* name);

// No error dialogs while the slave exits (filters failing in their destructors): on Windows the
// system error boxes, unhandled exceptions and the abort message of every loaded VC runtime.
void suppress_error_dialogs();

// ends the process at once (no destructors, no atexit handlers)
[[noreturn]] void exit_process(int exit_code);

// Ends the slave when the master process ends unexpectedly, while the slave is still working
// (before the end of its input). Windows: nothing to do, the master's job object kills it.
void exit_when_master_ends();

// Removes the named objects (events, shared memory) whose names start with prefix: what slaves
// killed before their cleanup left behind. Windows: nothing to do, the system removes an object
// with its last handle. POSIX: the names in /dev/shm; mapped views stay valid.
void remove_named_objects(const sys_string& prefix);

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
        VIEW_FAILED      // created / opened, but not mapped (e.g. out of address space)
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
    uint64_t _size;  // POSIX: for munmap
    bool _owner;     // POSIX: the creator removes the name when closing
    sys_string _name;
};

} // namespace platform
