// AVS_SplitProcess.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <string.h>
#include "avisynth.h"
#include "splitprocess.h"
#include <memory>
#include <assert.h>
#include <io.h>
#include <stdlib.h>
#include <fcntl.h>
#include <Psapi.h>

#include "../AVS_SplitProcess/statement.h"

typedef IScriptEnvironment* (__stdcall *CreateScriptEnvironment_t)(int);

typedef unsigned int (__cdecl *_set_abort_behavior_t)(unsigned int _Flags, unsigned int _Mask);
typedef int (__cdecl *_set_error_mode_t)(int _Mode);

#define DLL_STATEMENT_START "^\\s*### dll:"
#define DLL_STATEMENT_PARAM "\\s*(.*?)\\s*$"

#define DISABLE_EXIT_HACK_STATEMENT "^\\s*### disable exit hack"

LONG WINAPI exit_on_unhandled_exception(struct _EXCEPTION_POINTERS *ExceptionInfo) {
    ExitProcess(255);
    return EXCEPTION_EXECUTE_HANDLER;
}

DWORD WINAPI exit_on_timeout(void*) {
    Sleep(15000);
    ExitProcess(253);
}

void error_box(char* format, ...) {
    char message[16384];
    memset(message, 0, sizeof(message));
    va_list va;
    va_start(va, format);
    vsnprintf(message, sizeof(message)-1, format, va);
    va_end(va);
    MessageBoxA(NULL, message, "Message", MB_OK);
}

int silent_error_messages() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(exit_on_unhandled_exception);
    _set_abort_behavior(0, _CALL_REPORTFAULT | _WRITE_ABORT_MSG);
    _set_error_mode(_OUT_TO_STDERR);

    // Dirty hack to silent abort message box by other versions of VC runtime
    DWORD bytes_needed = 0;
    HMODULE modules[16384];
    memset(modules, 0, sizeof(modules));
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes_needed)) {
#ifdef _DEBUG
        error_box("EnumProcessModules failed (%d)", GetLastError());
#endif
        return 1;
    }
    for (size_t i = 0; i < bytes_needed / sizeof(HMODULE); i++) {
        char module_file_name[MAX_PATH + 1];
        memset(module_file_name, 0, sizeof(module_file_name));
        if (!GetModuleFileNameA(modules[i], module_file_name, MAX_PATH)) {
#ifdef _DEBUG
            error_box("GetModuleFileName failed (%d, %p)", GetLastError(), modules[i]);
#endif
            continue;
        }
        char* pos = strrchr(module_file_name, '\\');
        char* name = pos ? pos + 1 : module_file_name;
        if (_strnicmp(name, "msvcr", 5) != 0) {
            // Not VC runtime
            continue;
        }
        _set_abort_behavior_t sab = (_set_abort_behavior_t)GetProcAddress(modules[i], "_set_abort_behavior");
        if (sab) {
            sab(0, _CALL_REPORTFAULT | _WRITE_ABORT_MSG);
        }

        _set_error_mode_t sem = (_set_error_mode_t)GetProcAddress(modules[i], "_set_error_mode");
        if (sem) {
            sem(_OUT_TO_STDERR);
        }
    }
    return 0;
}

int _tmain(int argc, _TCHAR* argv[])
{
    HANDLE stdout_handle_original = GetStdHandle(STD_OUTPUT_HANDLE);
    assert(stdout_handle_original != INVALID_HANDLE_VALUE);
    HANDLE stdout_handle = INVALID_HANDLE_VALUE;
    DuplicateHandle(GetCurrentProcess(), stdout_handle_original, GetCurrentProcess(), &stdout_handle, 0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS);
    assert(stdout_handle != INVALID_HANDLE_VALUE);
    SetStdHandle(STD_OUTPUT_HANDLE, GetStdHandle(STD_ERROR_HANDLE));

    auto stdout_file = _fdopen(_open_osfhandle((intptr_t)stdout_handle, _O_TEXT), "w");
    TCHAR script[32768];

    memset(script, 0, sizeof(script));
    if (!GetEnvironmentVariable(SCRIPT_VAR_NAME, script, ARRAYSIZE(script) - 1))
    {
      fprintf(stdout_file, "GetEnvironmentVariable failed, code = %d.\n", GetLastError());
        return 1;
    }
    if (strlen(script) == 0)
    {
      fprintf(stdout_file, "Script is empty.\n");
        return 2;
    }
    HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    if (!stdin_handle)
    {
      fprintf(stdout_file, "Standard input not redirected.\n");
        return 5;
    } else if (stdin_handle == INVALID_HANDLE_VALUE) {
      fprintf(stdout_file, "GetStdHandle failed, code = %d.\n", GetLastError());
        return 6;
    }
    const TCHAR* avisynth_module_name = TEXT("avisynth.dll"); // Default
    scan_statement(script, DLL_STATEMENT_START DLL_STATEMENT_PARAM, NULL,
        NULL, &avisynth_module_name,
        NULL, NULL
    );

    HMODULE avisynth_module = LoadLibrary(avisynth_module_name);
    if (!avisynth_module)
    {
        fprintf(stdout_file, "Unable to load %s, code = %d\n", avisynth_module_name, GetLastError());
        return 9;
    }
    CreateScriptEnvironment_t create_script_env = (CreateScriptEnvironment_t)GetProcAddress(avisynth_module, "CreateScriptEnvironment");
    if (!create_script_env)
    {
        fprintf(stdout_file, "%s doesn't contain entry point of CreateScriptEnvironment, code = %d\n", avisynth_module_name, GetLastError());
        return 10;
    }

    const int ASK_FOR_INTERFACE_VERSION = AVISYNTH_CLASSIC_INTERFACE_VERSION; // AVS2.6 and AVS+ 3.5.0: 6, AVS+ 3.5.1: 6
    IScriptEnvironment* env = create_script_env(ASK_FOR_INTERFACE_VERSION);
    {
        AVS_linkage = env->GetAVSLinkage();

        AVSValue v;
        AVSValue script_value = script;
        try
        {
          v = env->Invoke("Eval", script_value);
        }
        catch (IScriptEnvironment::NotFound)
        {
            fprintf(stdout_file, "Unexpected exception: Filter not found.\n");
            fflush(stdout_file);
            return 3;
        }
        catch (AvisynthError e)
        {
            fprintf(stdout_file, "Script error: %s\n", e.msg);
            fflush(stdout_file);
            return 4;
        }
        catch (...)
        {
            fprintf(stdout_file, "Unknown exception\n");
            fflush(stdout_file);
            return 7;
        }
        fprintf(stdout_file, SLAVE_OK_FLAG "\n");
        fflush(stdout_file);
        char dummy_buffer[256];
        DWORD bytes_read = 0;
        do
        {
            if (!ReadFile(stdin_handle, dummy_buffer, sizeof(dummy_buffer), &bytes_read, NULL))
            {
                fprintf(stdout_file, "ReadFile failed, code = %d.\n", GetLastError());
                break;
            }
        } while (bytes_read > 0);
        fprintf(stdout_file, "Terminating...\n");
        fflush(stdout_file);

#ifndef _DEBUG
        // Sometimes filters have problem on destruction, try to silent them
        if (!has_statement(script, DISABLE_EXIT_HACK_STATEMENT)) {
            silent_error_messages();
        }

        // Not sure why slave processes do not exit sometimes... here is another hack
        CreateThread(NULL, 0, exit_on_timeout, NULL, 0, NULL);
#endif
    }
    env->DeleteScriptEnvironment();

    // To prevent problems we don't free it, since we are exiting anyways
    // FreeLibrary(avisynth_module);
    ExitProcess(0);
    return 0;
}

// 2.6+ interface: this one should be filled manually after getting env
const AVS_Linkage* AVS_linkage = nullptr;

