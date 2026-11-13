// Win32 implementation of the slave program part of platform.h (linked into the slave only).
// Windows XP compatible: no Vista or newer API.

#include "common.h"
#include "platform.h"

#include <fcntl.h>
#include <io.h>
#include <Psapi.h>
#include <stdarg.h>
#include <sstream>
#include <string.h>

namespace platform {

static std::string error_text(const char* call, DWORD code)
{
    std::ostringstream ss;
    ss << call << " failed, code = " << code;
    return ss.str();
}

FILE* take_over_stdout()
{
    HANDLE stdout_handle_original = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdout_handle_original == INVALID_HANDLE_VALUE || !stdout_handle_original)
    {
        return NULL;
    }
    // our own, not inheritable handle of the pipe; the original one is closed
    HANDLE stdout_handle = INVALID_HANDLE_VALUE;
    if (!DuplicateHandle(GetCurrentProcess(), stdout_handle_original, GetCurrentProcess(), &stdout_handle, 0, FALSE,
        DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
    {
        return NULL;
    }
    // what filters print to stdout goes to stderr (the master reads the same pipe, but not
    // through our stream's buffer)
    SetStdHandle(STD_OUTPUT_HANDLE, GetStdHandle(STD_ERROR_HANDLE));
    const int fd = _open_osfhandle((intptr_t)stdout_handle, _O_TEXT);
    if (fd < 0)
    {
        return NULL;
    }
    return _fdopen(fd, "w");
}

bool check_input(std::string& error_message)
{
    HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    if (!stdin_handle)
    {
        error_message = "Standard input not redirected.";
        return false;
    }
    if (stdin_handle == INVALID_HANDLE_VALUE)
    {
        error_message = error_text("GetStdHandle", GetLastError());
        return false;
    }
    return true;
}

bool wait_for_end_of_input(std::string& error_message)
{
    HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    char buffer[256];
    DWORD bytes_read = 0;
    do
    {
        if (!ReadFile(stdin_handle, buffer, sizeof(buffer), &bytes_read, NULL))
        {
            const DWORD code = GetLastError();
            if (code == ERROR_BROKEN_PIPE)
            {
                return true; // the master closed its end: the normal way to end
            }
            error_message = error_text("ReadFile", code);
            return false;
        }
    } while (bytes_read > 0);
    return true;
}

void* load_library(const char* name, std::string& error_message)
{
    HMODULE module = LoadLibraryA(name);
    if (!module)
    {
        error_message = error_text("LoadLibrary", GetLastError());
    }
    return module;
}

void* library_symbol(void* library, const char* name)
{
    return (void*)GetProcAddress((HMODULE)library, name);
}

// --- error dialog suppression ------------------------------------------------------------------

typedef unsigned int (__cdecl *_set_abort_behavior_t)(unsigned int _Flags, unsigned int _Mask);
typedef int (__cdecl *_set_error_mode_t)(int _Mode);

static LONG WINAPI exit_on_unhandled_exception(struct _EXCEPTION_POINTERS*)
{
    ExitProcess(255);
    return EXCEPTION_EXECUTE_HANDLER;
}

#ifdef _DEBUG
static void error_box(const char* format, ...)
{
    char message[16384];
    va_list va;
    va_start(va, format);
    vsnprintf(message, sizeof(message), format, va);
    va_end(va);
    MessageBoxA(NULL, message, "Message", MB_OK);
}
#endif

void suppress_error_dialogs()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(exit_on_unhandled_exception);
#if defined(_MSC_VER) || defined(_UCRT)
    // the old msvcrt.dll of MinGW (MSYS2 MINGW32 / MINGW64) has no _set_abort_behavior;
    // the loop below sets it in every loaded VC runtime anyway
    _set_abort_behavior(0, _CALL_REPORTFAULT | _WRITE_ABORT_MSG);
#endif
    _set_error_mode(_OUT_TO_STDERR);

    // Dirty hack to silence the abort message box of the other versions of the VC runtime
    DWORD bytes_needed = 0;
    static HMODULE modules[16384];
    memset(modules, 0, sizeof(modules));
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes_needed))
    {
#ifdef _DEBUG
        error_box("EnumProcessModules failed (%lu)", GetLastError());
#endif
        return;
    }
    for (size_t i = 0; i < bytes_needed / sizeof(HMODULE); i++)
    {
        char module_file_name[MAX_PATH + 1];
        memset(module_file_name, 0, sizeof(module_file_name));
        if (!GetModuleFileNameA(modules[i], module_file_name, MAX_PATH))
        {
#ifdef _DEBUG
            error_box("GetModuleFileName failed (%lu, %p)", GetLastError(), (void*)modules[i]);
#endif
            continue;
        }
        const char* pos = strrchr(module_file_name, '\\');
        const char* name = pos ? pos + 1 : module_file_name;
        if (_strnicmp(name, "msvcr", 5) != 0)
        {
            // not a VC runtime
            continue;
        }
        _set_abort_behavior_t sab = (_set_abort_behavior_t)GetProcAddress(modules[i], "_set_abort_behavior");
        if (sab)
        {
            sab(0, _CALL_REPORTFAULT | _WRITE_ABORT_MSG);
        }
        _set_error_mode_t sem = (_set_error_mode_t)GetProcAddress(modules[i], "_set_error_mode");
        if (sem)
        {
            sem(_OUT_TO_STDERR);
        }
    }
}

void exit_process(int exit_code)
{
    ExitProcess((UINT)exit_code);
}

} // namespace platform
