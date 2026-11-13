#include "common.h"
#include "Lock.h"
#include <assert.h>

#define DISABLE_TRACING
#define TRACE_PREFIX __TRACE_TEXT("Lock")
#include "trace.h"

CriticalSectionLock::CriticalSectionLock()
{
    memset(&_section, 0, sizeof(_section));
    InitializeCriticalSection(&_section);
}

CriticalSectionLock::~CriticalSectionLock()
{
    DeleteCriticalSection(&_section);
}

#ifdef _DEBUG
bool CriticalSectionLock::IsOwnedLock()
{
    return GetCurrentThreadId() == _owning_thread_id;
}
#endif

void CriticalSectionLock::Lock()
{
    TRACE("Enter lock");
#ifdef _DEBUG
    int lock_start_time = GetTickCount();
#endif
    EnterCriticalSection(&_section);
#ifdef _DEBUG
    _owning_thread_id = GetCurrentThreadId();
    _enter_time = GetTickCount();
    if (_enter_time - lock_start_time > 5000)
    {
        assert(("We took more than 5 second to take the lock!", false));
    }
#endif
}

void CriticalSectionLock::Unlock()
{
#ifdef _DEBUG
    if (GetTickCount() - _enter_time > 1000)
    {
        assert(("The locking period is too long!", false));
    }
    _owning_thread_id = 0;
#endif
    LeaveCriticalSection(&_section);
    TRACE("Exit lock");
}