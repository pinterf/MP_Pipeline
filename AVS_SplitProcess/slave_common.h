#pragma once

#include <Windows.h>
#include "avisynth.h"
#include "utils.h"

static const int MAX_SLAVES = 255;


#define MAX_PLATFORM_LENGTH 16

#define GET_OUTPUT_PORT_FUNCTION_NAME "MPP_GetOutputPort"

#if defined(_M_IX86)

#define CURRENT_PLATFORM "win32"

#elif defined(_M_X64)

#define CURRENT_PLATFORM "win64"

#else

#error Unknown platform.

#endif

static void append_platform_if_needed(TCHAR* path, const char* platform) 
{
    if (strlen(platform) > 0 && _stricmp(platform, CURRENT_PLATFORM) != 0)
    {
        tprintf_append(path, TEXT(".%hs"), platform);
    }
}

typedef struct _slave_create_params
{
    const char* filter_name;
    char* script;
    const wchar_t* script_name_vars; // SCRIPT_NAME_VARS_VAR_NAME_W content, NULL: none
    char slave_platform[MAX_PLATFORM_LENGTH + 1];
    HANDLE slave_job_object;
} slave_create_params;

void create_slave(IScriptEnvironment* env, slave_create_params* params, int* new_slave_port, HANDLE* slave_stdin_handle);