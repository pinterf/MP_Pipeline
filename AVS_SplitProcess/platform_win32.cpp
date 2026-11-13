// Win32 implementation of platform.h. Windows XP compatible: no Vista or newer API.

#define UNICODE
#define _UNICODE

#include "common.h"
#include "platform.h"

#include <rpc.h>
#include <stdlib.h>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace platform {

// --- processes and threads ---------------------------------------------------------------------

uint32_t current_process_id()
{
    return GetCurrentProcessId();
}

uint32_t current_thread_id()
{
    return GetCurrentThreadId();
}

bool is_process_alive(uint32_t process_id)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, process_id);
    if (!process)
    {
        // no such process (any more), or no access: treated as gone, as before
        return false;
    }
    DWORD exit_code = 0;
    const BOOL result = GetExitCodeProcess(process, &exit_code);
    CloseHandle(process);
    return result && exit_code == STILL_ACTIVE;
}

// --- processors --------------------------------------------------------------------------------

int processor_count()
{
    SYSTEM_INFO si;
    memset(&si, 0, sizeof(si));
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (int)si.dwNumberOfProcessors : 1;
}

// --- misc --------------------------------------------------------------------------------------

std::string new_unique_id()
{
    UUID uuid;
    if (UuidCreate(&uuid) != RPC_S_OK)
    {
        return std::string();
    }
    RPC_CSTR text = NULL;
    if (UuidToStringA(&uuid, &text) != RPC_S_OK || !text)
    {
        return std::string();
    }
    std::string result((const char*)text);
    RpcStringFreeA(&text);
    return result;
}

void debug_output(const char* text)
{
    OutputDebugStringA(text);
}

// --- text --------------------------------------------------------------------------------------

static UINT ansi_code_page()
{
    return AreFileApisANSI() ? CP_ACP : CP_OEMCP;
}

static std::wstring multibyte_to_wide(const std::string& s, UINT code_page)
{
    if (s.empty()) return std::wstring();
    const int len = MultiByteToWideChar(code_page, 0, s.data(), (int)s.size(), NULL, 0);
    std::wstring result(len, L'\0');
    MultiByteToWideChar(code_page, 0, s.data(), (int)s.size(), &result[0], len);
    return result;
}

static std::string wide_to_multibyte(const std::wstring& s, UINT code_page)
{
    if (s.empty()) return std::string();
    const int len = WideCharToMultiByte(code_page, 0, s.data(), (int)s.size(), NULL, 0, NULL, NULL);
    std::string result(len, '\0');
    // characters the code page does not have become '?'
    WideCharToMultiByte(code_page, 0, s.data(), (int)s.size(), &result[0], len, NULL, NULL);
    return result;
}

std::string ansi_to_utf8(const std::string& s)
{
    return wide_to_multibyte(multibyte_to_wide(s, ansi_code_page()), CP_UTF8);
}

std::string utf8_to_ansi(const std::string& s)
{
    return wide_to_multibyte(multibyte_to_wide(s, CP_UTF8), ansi_code_page());
}

sys_string utf8_to_sys(const std::string& s)
{
    return multibyte_to_wide(s, CP_UTF8);
}

std::string sys_to_utf8(const sys_string& s)
{
    return wide_to_multibyte(s, CP_UTF8);
}

std::string sys_to_ansi(const sys_string& s)
{
    return wide_to_multibyte(s, ansi_code_page());
}

// --- files, modules, environment ---------------------------------------------------------------

sys_string module_path()
{
    HMODULE module = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&module_path, &module))
    {
        return sys_string();
    }
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true)
    {
        const DWORD len = GetModuleFileNameW(module, &buffer[0], (DWORD)buffer.size());
        if (len == 0)
        {
            return sys_string();
        }
        if (len < buffer.size())
        {
            return sys_string(&buffer[0], len);
        }
        // truncated (XP does not report it with an error): try a larger buffer
        if (buffer.size() >= 32768)
        {
            return sys_string();
        }
        buffer.resize(buffer.size() * 2);
    }
}

bool file_exists(const sys_string& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool get_environment_variable(const char* name, std::string& value)
{
    const DWORD size = GetEnvironmentVariableA(name, NULL, 0);
    if (size == 0)
    {
        return false;
    }
    std::vector<char> buffer(size + 1, 0);
    const DWORD len = GetEnvironmentVariableA(name, &buffer[0], size);
    if (len == 0 || len >= size + 1)
    {
        return false;
    }
    value.assign(&buffer[0], len);
    return true;
}

bool get_environment_variable(const SYSCHAR* name, sys_string& value)
{
    const DWORD size = GetEnvironmentVariableW(name, NULL, 0);
    if (size == 0)
    {
        return false;
    }
    std::vector<wchar_t> buffer(size + 1, 0);
    const DWORD len = GetEnvironmentVariableW(name, &buffer[0], size);
    if (len == 0 || len >= size + 1)
    {
        return false;
    }
    value.assign(&buffer[0], len);
    return true;
}

bool set_environment_variable(const char* name, const char* value)
{
    return SetEnvironmentVariableA(name, value) != 0;
}

bool set_environment_variable(const SYSCHAR* name, const SYSCHAR* value)
{
    return SetEnvironmentVariableW(name, value) != 0;
}

// --- child processes ---------------------------------------------------------------------------

static std::string error_text(const char* call, DWORD code)
{
    std::ostringstream ss;
    ss << call << " failed, code = " << code;
    return ss.str();
}

static void close_handle(void*& handle)
{
    if (handle)
    {
        CloseHandle((HANDLE)handle);
        handle = NULL;
    }
}

ChildProcessGroup::ChildProcessGroup() : _job(NULL), _kill_on_destroy(true)
{
    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (!job)
    {
        throw std::runtime_error(error_text("CreateJobObject", GetLastError()));
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_info;
    memset(&limit_info, 0, sizeof(limit_info));
    limit_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit_info, sizeof(limit_info)))
    {
        const DWORD code = GetLastError();
        CloseHandle(job);
        throw std::runtime_error(error_text("SetInformationJobObject", code));
    }
    _job = job;
}

ChildProcessGroup::~ChildProcessGroup()
{
    if (!_kill_on_destroy)
    {
        // a normal end: lift kill-on-close, so the slaves can clean up and terminate themselves
        // and the master does not need to wait for them
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_info;
        memset(&limit_info, 0, sizeof(limit_info));
        SetInformationJobObject((HANDLE)_job, JobObjectExtendedLimitInformation, &limit_info, sizeof(limit_info));
    }
    // with kill-on-close still set, closing the last handle of the job kills its processes
    close_handle(_job);
}

void ChildProcessGroup::set_kill_on_destroy(bool kill)
{
    _kill_on_destroy = kill;
}

ChildInput::~ChildInput()
{
    close_handle(_handle);
}

ChildProcess::ChildProcess() : _process(NULL), _input_write(NULL), _output_read(NULL)
{
}

ChildProcess::~ChildProcess()
{
    close_handle(_process);
    close_handle(_input_write);
    close_handle(_output_read);
}

bool ChildProcess::start(const sys_string& executable, ChildProcessGroup& group, std::string& error_message)
{
    HANDLE input_read = NULL, input_write = NULL, output_read = NULL, output_write = NULL;
    // the child's ends of the pipes are inherited, ours are not
    SECURITY_ATTRIBUTES pipe_attrs;
    memset(&pipe_attrs, 0, sizeof(pipe_attrs));
    pipe_attrs.nLength = sizeof(pipe_attrs);
    pipe_attrs.bInheritHandle = TRUE;
    auto close_child_ends = [&] {
        if (input_read) CloseHandle(input_read);
        if (output_write) CloseHandle(output_write);
        input_read = output_write = NULL;
    };
    if (!CreatePipe(&input_read, &input_write, &pipe_attrs, 0))
    {
        error_message = error_text("CreatePipe", GetLastError());
        return false;
    }
    _input_write = input_write;
    if (!SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0))
    {
        error_message = error_text("SetHandleInformation", GetLastError());
        close_child_ends();
        return false;
    }
    if (!CreatePipe(&output_read, &output_write, &pipe_attrs, 0))
    {
        error_message = error_text("CreatePipe", GetLastError());
        close_child_ends();
        return false;
    }
    _output_read = output_read;
    if (!SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0))
    {
        error_message = error_text("SetHandleInformation", GetLastError());
        close_child_ends();
        return false;
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput = input_read;
    si.hStdError = output_write;
    si.hStdOutput = output_write;
    si.wShowWindow = SW_HIDE;
    // CREATE_BREAKAWAY_FROM_JOB: the master itself may be in a job (placed there by Windows or
    // the host); without the flag the child would be in that job too, and assigning it to the
    // group's job would fail
    BOOL created = CreateProcessW(executable.c_str(), NULL, NULL, NULL, TRUE,
        CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, NULL, NULL, &si, &pi);
    if (!created && GetLastError() == ERROR_ACCESS_DENIED)
    {
        // the master's job does not allow breaking away (no JOB_OBJECT_LIMIT_BREAKAWAY_OK)
        created = CreateProcessW(executable.c_str(), NULL, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    }
    const DWORD create_error = GetLastError();
    close_child_ends();
    if (!created)
    {
        error_message = error_text("CreateProcess", create_error);
        return false;
    }
    CloseHandle(pi.hThread);
    _process = pi.hProcess;
    // a failure is no error: the slave is just not killed when the master ends unexpectedly
    AssignProcessToJobObject((HANDLE)group._job, pi.hProcess);
    return true;
}

size_t ChildProcess::read_output(char* buffer, size_t size)
{
    DWORD bytes_read = 0;
    if (!_output_read || size == 0 || !ReadFile((HANDLE)_output_read, buffer, (DWORD)size, &bytes_read, NULL))
    {
        return 0;
    }
    return bytes_read;
}

bool ChildProcess::wait_for_exit(unsigned ms_timeout, uint32_t& exit_code)
{
    if (!_process)
    {
        return false;
    }
    WaitForSingleObject((HANDLE)_process, ms_timeout == WAIT_INFINITE ? INFINITE : (DWORD)ms_timeout);
    DWORD code = 0;
    if (!GetExitCodeProcess((HANDLE)_process, &code) || code == STILL_ACTIVE)
    {
        return false;
    }
    exit_code = code;
    return true;
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

// --- named events ------------------------------------------------------------------------------

NamedEvent::NamedEvent(const sys_string& name, bool create, bool manual_reset, bool initially_set) :
    _handle(NULL)
{
    SetLastError(ERROR_SUCCESS);
    HANDLE handle = create
        ? CreateEventW(NULL, manual_reset ? TRUE : FALSE, initially_set ? TRUE : FALSE, name.c_str())
        : OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, name.c_str());
    if (!handle)
    {
        throw std::runtime_error(create ? "Unable to create event." : "Unable to open event.");
    }
    if (create && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(handle);
        throw std::runtime_error("The event already exists.");
    }
    _handle = handle;
}

NamedEvent::~NamedEvent()
{
    CloseHandle((HANDLE)_handle);
}

void NamedEvent::set()
{
    SetEvent((HANDLE)_handle);
}

void NamedEvent::reset()
{
    ResetEvent((HANDLE)_handle);
}

bool NamedEvent::wait(unsigned ms_timeout)
{
    const DWORD result = WaitForSingleObject((HANDLE)_handle, ms_timeout == WAIT_INFINITE ? INFINITE : (DWORD)ms_timeout);
    switch (result)
    {
    case WAIT_OBJECT_0:
        return true;
    case WAIT_TIMEOUT:
        return false;
    default:
        std::ostringstream error_msg;
        error_msg << "Error when waiting on the event, return value: " << result << " error code: " << GetLastError();
        throw std::runtime_error(error_msg.str());
    }
}

// --- named shared memory -----------------------------------------------------------------------

SharedMapping::SharedMapping() : _handle(NULL), _view(nullptr)
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
        UnmapViewOfFile(_view);
        _view = nullptr;
    }
    if (_handle)
    {
        CloseHandle((HANDLE)_handle);
        _handle = NULL;
    }
}

SharedMapping::Result SharedMapping::create(const sys_string& name, uint64_t size)
{
    close();
    SetLastError(ERROR_SUCCESS);
    HANDLE handle = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        (DWORD)(size >> 32), (DWORD)(size & 0xFFFFFFFF), name.c_str());
    if (!handle)
    {
        return FAILED;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(handle);
        return ALREADY_EXISTS;
    }
    _handle = handle;
    _view = (uint8_t*)MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, 0);
    if (!_view)
    {
        close();
        return MAP_FAILED;
    }
    return OK;
}

SharedMapping::Result SharedMapping::open(const sys_string& name, bool writable)
{
    close();
    HANDLE handle = OpenFileMappingW(writable ? FILE_MAP_WRITE : FILE_MAP_READ, FALSE, name.c_str());
    if (!handle)
    {
        return NOT_FOUND;
    }
    _handle = handle;
    _view = (uint8_t*)MapViewOfFile(handle, writable ? FILE_MAP_WRITE : FILE_MAP_READ, 0, 0, 0);
    if (!_view)
    {
        close();
        return MAP_FAILED;
    }
    return OK;
}

} // namespace platform
