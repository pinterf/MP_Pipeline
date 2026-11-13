// Win32 implementation of platform.h. Windows XP compatible: no Vista or newer API.

#define UNICODE
#define _UNICODE

#include "common.h"
#include "platform.h"

#include <rpc.h>
#include <stdlib.h>
#include <sstream>
#include <stdexcept>

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
