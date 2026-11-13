#include "common.h"
#include "trace.h"
#include "platform.h"

#include <stdarg.h>
#include <stdio.h>

void _trace(const char* format, ...)
{
    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    platform::debug_output(buffer);
    printf("%s\n", buffer);
}