#pragma once

#include <memory>
#include <string>
#include <string.h>

#include "avisynth.h"
#include "platform.h"
#include "utils.h"

static const int MAX_SLAVES = 255;

// clips a process can export besides its result (### export clip)
static const int MAX_EXPORTED_CLIPS = 8;


#define MAX_PLATFORM_LENGTH 16

#define GET_OUTPUT_PORT_FUNCTION_NAME "MPP_GetOutputPort"

#if !defined(_WIN32)

// "### platform:" is Windows only (see platform::supports_other_platforms)
#define CURRENT_PLATFORM "native"

#elif defined(_M_IX86) || defined(__i386__)

#define CURRENT_PLATFORM "win32"

#elif defined(_M_X64) || defined(__x86_64__)

#define CURRENT_PLATFORM "win64"

#elif defined(_M_ARM64) || defined(__aarch64__)

#define CURRENT_PLATFORM "arm64"

#else

#error Unknown platform.

#endif

// The file name suffix of a platform's files next to the plugin: ".win32" / ".win64" for the other
// bitness (MP_Pipeline.dll.win32, MP_Pipeline.dll.win32.slave.exe), empty for the current platform.
inline std::string platform_file_suffix(const char* platform)
{
    if (!platform || !platform[0] || streqi(platform, CURRENT_PLATFORM))
    {
        return std::string();
    }
    return std::string(".") + platform;
}

typedef struct _slave_create_params
{
    const char* filter_name;
    char* script;
    const SYSCHAR* script_name_vars; // SCRIPT_NAME_VARS_VAR_NAME content, NULL: none
    char slave_platform[MAX_PLATFORM_LENGTH + 1];
    platform::ChildProcessGroup* slave_group;
} slave_create_params;

// starts a slave process for the script; slave_input: its stdin, the slave runs while it is kept
void create_slave(IScriptEnvironment* env, slave_create_params* params, int* new_slave_port, std::unique_ptr<platform::ChildInput>& slave_input);