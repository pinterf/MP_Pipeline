#include "common.h"
#include "SpinLock.h"

int _processor_count = 0;

void fill_processor_count()
{
    _processor_count = platform::processor_count();
}