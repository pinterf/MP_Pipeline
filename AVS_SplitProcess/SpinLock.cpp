#include "common.h"
#include "SpinLock.h"

int _processor_count = 0;

void fill_processor_count()
{
    SYSTEM_INFO si;
    memset(&si, 0, sizeof(si));
    GetSystemInfo(&si);
    assert(si.dwNumberOfProcessors);
    _processor_count = si.dwNumberOfProcessors;
}