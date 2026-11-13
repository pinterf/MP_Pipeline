// POSIX (Linux) implementation of the slave program part of platform.h (linked into the slave only).

#include "common.h"
#include "platform.h"

#include <atomic>
#include <chrono>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <unistd.h>

namespace platform {

// set when the master closed our stdin: from then on the slave ends on its own
static std::atomic<bool> input_ended(false);

FILE* take_over_stdout()
{
    // the master closes its end of our output pipe after the handshake: writing to it must not
    // kill the process (filters printing later), the write just fails
    signal(SIGPIPE, SIG_IGN);
    const int fd = dup(STDOUT_FILENO);
    if (fd < 0)
    {
        return NULL;
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    // what filters print to stdout goes to stderr (the master reads the same pipe, but not
    // through our stream's buffer)
    dup2(STDERR_FILENO, STDOUT_FILENO);
    return fdopen(fd, "w");
}

bool check_input(std::string& error_message)
{
    if (fcntl(STDIN_FILENO, F_GETFD) < 0)
    {
        error_message = "Standard input not available.";
        return false;
    }
    return true;
}

bool wait_for_end_of_input(std::string& error_message)
{
    char buffer[256];
    while (true)
    {
        const ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (n > 0)
        {
            continue;
        }
        if (n < 0 && errno == EINTR)
        {
            continue;
        }
        input_ended = true;
        if (n < 0)
        {
            error_message = std::string("read failed: ") + strerror(errno);
            return false;
        }
        return true; // the master closed its end: the normal way to end
    }
}

void* load_library(const char* name, std::string& error_message)
{
    void* library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (!library)
    {
        const char* error = dlerror();
        error_message = error ? error : "dlopen failed";
    }
    return library;
}

void* library_symbol(void* library, const char* name)
{
    return dlsym(library, name);
}

void suppress_error_dialogs()
{
    // no error dialogs on POSIX
}

void exit_process(int exit_code)
{
    _exit(exit_code);
}

void exit_when_master_ends()
{
    const char* value = getenv("MPP_MASTER_PID");
    if (!value || !value[0])
    {
        return; // not started by MP_Pipeline
    }
    const pid_t master = (pid_t)atol(value);
    if (getppid() != master)
    {
        _exit(254); // the master is gone already
    }
    // When the master ends, the slave is reparented (to init or a subreaper): getppid() changes.
    // Unlike PR_SET_PDEATHSIG this does not depend on the master's thread that started us.
    // After the end of the input the slave finishes on its own (the watchdog in slave_main).
    std::thread([master] {
        while (!input_ended)
        {
            if (getppid() != master)
            {
                _exit(254);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }).detach();
}

} // namespace platform
