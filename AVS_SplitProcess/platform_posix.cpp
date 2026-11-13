// POSIX (Linux) implementation of platform.h.
//
// Named objects (events, shared memory) are POSIX shared memory objects (shm_open), named
// "/mpp_<name>". The creator removes the name when it closes the object; the views mapped in
// other processes stay valid (like a Windows mapping lives while a view of it exists).
// Events: a process-shared, robust pthread mutex + condition variable in a small shared memory
// object, the same manual / auto reset semantics as a Win32 event.
// Text: everything is UTF-8, the ANSI conversions return their input.

#include "common.h"
#include "platform.h"

#include <atomic>
#include <chrono>
#include <errno.h>
#include <fcntl.h>
#include <functional>
#include <limits.h>
#include <mutex>
#include <pthread.h>
#include <random>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <stdlib.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <dirent.h>
#include <dlfcn.h>

extern char** environ;

namespace platform {

static std::string errno_text(const char* call, int error)
{
    return std::string(call) + " failed: " + strerror(error);
}

// "/mpp_<name>", only characters a shm name may contain
static std::string shm_name(const sys_string& name)
{
    std::string result = "/mpp_";
    for (char c : name)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        result += ok ? c : '_';
    }
    if (result.size() > NAME_MAX)
    {
        // keep it unique: the beginning and a hash of the whole
        const std::string hash = std::to_string(std::hash<std::string>()(result));
        result = result.substr(0, NAME_MAX - hash.size() - 1) + "_" + hash;
    }
    return result;
}

// --- processes and threads ---------------------------------------------------------------------

uint32_t current_process_id()
{
    return (uint32_t)getpid();
}

uint32_t current_thread_id()
{
    return (uint32_t)std::hash<std::thread::id>()(std::this_thread::get_id());
}

bool is_process_alive(uint32_t process_id)
{
    if (kill((pid_t)process_id, 0) != 0 && errno != EPERM)
    {
        return false;
    }
    // an exited process not yet reaped by its parent (zombie) still exists for kill()
    char path[64];
    snprintf(path, sizeof(path), "/proc/%u/stat", process_id);
    FILE* f = fopen(path, "r");
    if (!f)
    {
        return true; // no /proc: trust kill()
    }
    char buffer[512] = { 0 };
    const size_t len = fread(buffer, 1, sizeof(buffer) - 1, f);
    fclose(f);
    buffer[len] = 0;
    // "<pid> (<comm>) <state> ...": the state follows the last ')'
    const char* p = strrchr(buffer, ')');
    return !(p && p[1] == ' ' && (p[2] == 'Z' || p[2] == 'X'));
}

// --- processors --------------------------------------------------------------------------------

int processor_count()
{
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

// --- misc --------------------------------------------------------------------------------------

std::string new_unique_id()
{
    unsigned char bytes[16];
    bool ok = false;
    const int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0)
    {
        ok = read(fd, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes);
        close(fd);
    }
    if (!ok)
    {
        std::random_device rd;
        for (unsigned char& b : bytes)
        {
            b = (unsigned char)rd();
        }
    }
    // a UUID's text form: 8-4-4-4-12 hex digits
    char text[40];
    snprintf(text, sizeof(text), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    return text;
}

void debug_output(const char* text)
{
    fprintf(stderr, "%s\n", text);
}

// --- text --------------------------------------------------------------------------------------

std::string ansi_to_utf8(const std::string& s) { return s; }
std::string utf8_to_ansi(const std::string& s) { return s; }
sys_string utf8_to_sys(const std::string& s) { return s; }
std::string sys_to_utf8(const sys_string& s) { return s; }
std::string sys_to_ansi(const sys_string& s) { return s; }

// --- files, modules, environment ---------------------------------------------------------------

sys_string module_path()
{
    Dl_info info;
    if (!dladdr((void*)&module_path, &info) || !info.dli_fname)
    {
        return sys_string();
    }
    char resolved[PATH_MAX];
    if (realpath(info.dli_fname, resolved))
    {
        return resolved;
    }
    return info.dli_fname;
}

bool file_exists(const sys_string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool get_environment_variable(const char* name, std::string& value)
{
    const char* v = getenv(name);
    if (!v || !v[0])
    {
        return false;
    }
    value = v;
    return true;
}

bool set_environment_variable(const char* name, const char* value)
{
    return value ? setenv(name, value, 1) == 0 : unsetenv(name) == 0;
}

const char* slave_executable_suffix()
{
    return ".slave";
}

const char* default_avisynth_library()
{
    return "libavisynth.so";
}

bool supports_other_platforms()
{
    return false;
}

// --- child processes ---------------------------------------------------------------------------

// file descriptors in the void* members: fd + 1, so that NULL means none
static void* fd_handle(int fd) { return (void*)(intptr_t)(fd + 1); }
static int handle_fd(void* handle) { return (int)(intptr_t)handle - 1; }

static void close_fd_handle(void*& handle)
{
    if (handle)
    {
        close(handle_fd(handle));
        handle = NULL;
    }
}

struct ChildGroupData
{
    std::mutex lock;
    std::vector<pid_t> pids;
};

// Slaves of ended groups that may still be running (finishing on their own): reaped when they
// exit, at the next group construction or destruction. No thread for it: a thread running the
// plugin's code would crash when AviSynth unloads the plugin (dlclose) while it waits.
static std::mutex pending_lock;
static std::vector<pid_t> pending_pids;

static void reap_finished_children()
{
    std::lock_guard<std::mutex> guard(pending_lock);
    for (size_t i = 0; i < pending_pids.size();)
    {
        const pid_t r = waitpid(pending_pids[i], NULL, WNOHANG);
        if (r == pending_pids[i] || (r < 0 && errno == ECHILD))
        {
            pending_pids.erase(pending_pids.begin() + i); // reaped (or not ours any more)
        } else {
            i++;
        }
    }
}

ChildProcessGroup::ChildProcessGroup() : _job(new ChildGroupData()), _kill_on_destroy(true)
{
    reap_finished_children();
}

ChildProcessGroup::~ChildProcessGroup()
{
    ChildGroupData* data = (ChildGroupData*)_job;
    std::vector<pid_t> pids;
    {
        std::lock_guard<std::mutex> guard(data->lock);
        pids.swap(data->pids);
    }
    if (_kill_on_destroy)
    {
        for (pid_t pid : pids)
        {
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
        }
    } else if (!pids.empty()) {
        // a normal end: the slaves finish on their own after the end of their input; they are
        // reaped when they have exited (now or at the next group), not to stay zombies
        {
            std::lock_guard<std::mutex> guard(pending_lock);
            pending_pids.insert(pending_pids.end(), pids.begin(), pids.end());
        }
        reap_finished_children();
    }
    delete data;
}

void ChildProcessGroup::set_kill_on_destroy(bool kill)
{
    _kill_on_destroy = kill;
}

ChildInput::~ChildInput()
{
    close_fd_handle(_handle);
}

ChildProcess::ChildProcess() : _process(NULL), _input_write(NULL), _output_read(NULL)
{
}

ChildProcess::~ChildProcess()
{
    close_fd_handle(_input_write);
    close_fd_handle(_output_read);
    // the pid (_process) is the group's: it reaps or kills it
}

bool ChildProcess::start(const sys_string& executable, ChildProcessGroup& group, std::string& error_message)
{
    // close-on-exec: the child gets only its stdin / stdout / stderr copies (dup2 clears the flag)
    int input[2], output[2];
    if (pipe2(input, O_CLOEXEC) != 0)
    {
        error_message = errno_text("pipe2", errno);
        return false;
    }
    if (pipe2(output, O_CLOEXEC) != 0)
    {
        error_message = errno_text("pipe2", errno);
        close(input[0]);
        close(input[1]);
        return false;
    }
    _input_write = fd_handle(input[1]);
    _output_read = fd_handle(output[0]);

    // the slave watches its parent: when it is not the master any more, the master ended
    // (platform::exit_when_master_ends); the environment is process wide, the caller serializes
    setenv("MPP_MASTER_PID", std::to_string(getpid()).c_str(), 1);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, input[0], 0);
    posix_spawn_file_actions_adddup2(&actions, output[1], 1);
    posix_spawn_file_actions_adddup2(&actions, output[1], 2);
    char* argv[] = { const_cast<char*>(executable.c_str()), NULL };
    pid_t pid = 0;
    const int spawn_error = posix_spawn(&pid, executable.c_str(), &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(input[0]);
    close(output[1]);
    if (spawn_error != 0)
    {
        error_message = errno_text("posix_spawn", spawn_error);
        return false;
    }
    _process = (void*)(intptr_t)pid;
    ChildGroupData* data = (ChildGroupData*)group._job;
    std::lock_guard<std::mutex> guard(data->lock);
    data->pids.push_back(pid);
    return true;
}

size_t ChildProcess::read_output(char* buffer, size_t size)
{
    if (!_output_read || size == 0)
    {
        return 0;
    }
    while (true)
    {
        const ssize_t n = read(handle_fd(_output_read), buffer, size);
        if (n < 0 && errno == EINTR)
        {
            continue;
        }
        return n > 0 ? (size_t)n : 0;
    }
}

bool ChildProcess::wait_for_exit(unsigned ms_timeout, uint32_t& exit_code)
{
    if (!_process)
    {
        return false;
    }
    const pid_t pid = (pid_t)(intptr_t)_process;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms_timeout);
    while (true)
    {
        int status = 0;
        const pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
        {
            // a signal: 128 + its number, like a shell reports it
            exit_code = WIFEXITED(status) ? (uint32_t)WEXITSTATUS(status) : 128u + (uint32_t)WTERMSIG(status);
            return true;
        }
        if (r < 0 && errno != EINTR)
        {
            return false;
        }
        if (ms_timeout != WAIT_INFINITE && std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::unique_ptr<ChildInput> ChildProcess::take_input()
{
    if (!_input_write)
    {
        return std::unique_ptr<ChildInput>();
    }
    std::unique_ptr<ChildInput> input(new ChildInput(_input_write));
    _input_write = NULL;
    return input;
}

void remove_named_objects(const sys_string& prefix)
{
    // shm_open names are files in /dev/shm on Linux (without the leading '/')
    const std::string start = shm_name(prefix).substr(1);
    DIR* dir = opendir("/dev/shm");
    if (!dir)
    {
        return;
    }
    std::vector<std::string> names;
    while (struct dirent* entry = readdir(dir))
    {
        if (strncmp(entry->d_name, start.c_str(), start.size()) == 0)
        {
            names.push_back(std::string("/") + entry->d_name);
        }
    }
    closedir(dir);
    for (const std::string& name : names)
    {
        shm_unlink(name.c_str());
    }
}

// --- named events ------------------------------------------------------------------------------

struct SharedEventState
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int signaled;
    int manual_reset;
};

struct NamedEventData
{
    std::string name;
    bool owner;
    SharedEventState* state;
};

// a robust mutex: when a process dies holding it, the next one takes it over
static void lock_event(SharedEventState* state)
{
    if (pthread_mutex_lock(&state->mutex) == EOWNERDEAD)
    {
        pthread_mutex_consistent(&state->mutex);
    }
}

NamedEvent::NamedEvent(const sys_string& name, bool create, bool manual_reset, bool initially_set) :
    _handle(NULL)
{
    const std::string sname = shm_name(name);
    const int fd = shm_open(sname.c_str(), create ? (O_CREAT | O_EXCL | O_RDWR) : O_RDWR, 0600);
    if (fd < 0)
    {
        if (create && errno == EEXIST)
        {
            throw std::runtime_error("The event already exists.");
        }
        throw std::runtime_error(create ? "Unable to create event." : "Unable to open event.");
    }
    if (create && ftruncate(fd, sizeof(SharedEventState)) != 0)
    {
        close(fd);
        shm_unlink(sname.c_str());
        throw std::runtime_error("Unable to create event.");
    }
    void* view = mmap(NULL, sizeof(SharedEventState), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (view == MAP_FAILED)
    {
        if (create)
        {
            shm_unlink(sname.c_str());
        }
        throw std::runtime_error("Unable to map event.");
    }
    SharedEventState* state = (SharedEventState*)view;
    if (create)
    {
        pthread_mutexattr_t mattr;
        pthread_mutexattr_init(&mattr);
        pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&mattr, PTHREAD_MUTEX_ROBUST);
        pthread_mutex_init(&state->mutex, &mattr);
        pthread_mutexattr_destroy(&mattr);
        pthread_condattr_t cattr;
        pthread_condattr_init(&cattr);
        pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
        pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
        pthread_cond_init(&state->cond, &cattr);
        pthread_condattr_destroy(&cattr);
        state->signaled = initially_set ? 1 : 0;
        state->manual_reset = manual_reset ? 1 : 0;
    }
    _handle = new NamedEventData{ sname, create, state };
}

NamedEvent::~NamedEvent()
{
    NamedEventData* data = (NamedEventData*)_handle;
    // the mutex and the condition variable are not destroyed: another process may still use them
    munmap(data->state, sizeof(SharedEventState));
    if (data->owner)
    {
        shm_unlink(data->name.c_str());
    }
    delete data;
}

void NamedEvent::set()
{
    SharedEventState* state = ((NamedEventData*)_handle)->state;
    lock_event(state);
    state->signaled = 1;
    if (state->manual_reset)
    {
        pthread_cond_broadcast(&state->cond);
    } else {
        pthread_cond_signal(&state->cond);
    }
    pthread_mutex_unlock(&state->mutex);
}

void NamedEvent::reset()
{
    SharedEventState* state = ((NamedEventData*)_handle)->state;
    lock_event(state);
    state->signaled = 0;
    pthread_mutex_unlock(&state->mutex);
}

bool NamedEvent::wait(unsigned ms_timeout)
{
    SharedEventState* state = ((NamedEventData*)_handle)->state;
    struct timespec deadline;
    if (ms_timeout != WAIT_INFINITE)
    {
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_sec += ms_timeout / 1000;
        deadline.tv_nsec += (long)(ms_timeout % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L)
        {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
    }
    lock_event(state);
    int result = 0;
    while (!state->signaled)
    {
        result = ms_timeout == WAIT_INFINITE
            ? pthread_cond_wait(&state->cond, &state->mutex)
            : pthread_cond_timedwait(&state->cond, &state->mutex, &deadline);
        if (result == EOWNERDEAD)
        {
            pthread_mutex_consistent(&state->mutex);
            result = 0;
        }
        if (result == ETIMEDOUT)
        {
            break;
        }
        if (result != 0)
        {
            pthread_mutex_unlock(&state->mutex);
            throw std::runtime_error(errno_text("pthread_cond_wait", result));
        }
    }
    const bool signaled = state->signaled != 0;
    if (signaled && !state->manual_reset)
    {
        state->signaled = 0;
    }
    pthread_mutex_unlock(&state->mutex);
    return signaled;
}

// --- named shared memory -----------------------------------------------------------------------

SharedMapping::SharedMapping() : _handle(NULL), _view(nullptr), _size(0), _owner(false)
{
}

SharedMapping::~SharedMapping()
{
    close();
}

void SharedMapping::close()
{
    if (_view)
    {
        munmap(_view, (size_t)_size);
        _view = nullptr;
    }
    if (_owner)
    {
        shm_unlink(shm_name(_name).c_str());
        _owner = false;
    }
    _size = 0;
}

SharedMapping::Result SharedMapping::create(const sys_string& name, uint64_t size)
{
    close();
    const std::string sname = shm_name(name);
    const int fd = shm_open(sname.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0)
    {
        return errno == EEXIST ? ALREADY_EXISTS : FAILED;
    }
    // a new object is zero filled
    if (ftruncate(fd, (off_t)size) != 0)
    {
        ::close(fd);
        shm_unlink(sname.c_str());
        return FAILED;
    }
    void* view = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    if (view == MAP_FAILED)
    {
        shm_unlink(sname.c_str());
        return VIEW_FAILED;
    }
    _view = (uint8_t*)view;
    _size = size;
    _owner = true;
    _name = name;
    return OK;
}

SharedMapping::Result SharedMapping::open(const sys_string& name, bool writable)
{
    close();
    const int fd = shm_open(shm_name(name).c_str(), writable ? O_RDWR : O_RDONLY, 0);
    if (fd < 0)
    {
        return NOT_FOUND;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0)
    {
        ::close(fd);
        return VIEW_FAILED;
    }
    void* view = mmap(NULL, (size_t)st.st_size, writable ? (PROT_READ | PROT_WRITE) : PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (view == MAP_FAILED)
    {
        return VIEW_FAILED;
    }
    _view = (uint8_t*)view;
    _size = (uint64_t)st.st_size;
    _owner = false;
    _name = name;
    return OK;
}

} // namespace platform
