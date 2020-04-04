#define UNICODE
#define _UNICODE

#include "stdafx.h"

#include <Windows.h>
#include <stdio.h>

#include "slave_common.h"

#include "splitprocess.h"

#define TRACE_COMPONENT L"slave_common"
#include "trace.h"

static volatile HANDLE _startup_mutex = NULL;

static volatile LONG _port_counter = 1;

int get_unused_port()
{
    return InterlockedIncrement(&_port_counter);
}

#define TRACE_ERROR(format, ...) \
    do { \
        TRACE(format, __VA_ARGS__); \
        _snprintf(error_msg, error_msg_len, format, __VA_ARGS__); \
    } while (0)

#define CHECKED(name, ...) \
    do { \
        if (!name(__VA_ARGS__)) { \
            TRACE_ERROR("(slave_common.cpp) %hs failed, code = %d", #name, GetLastError()); \
            return NULL; \
        } \
    } while (0)

HANDLE create_slave_process(slave_create_params* params, char* error_msg, size_t error_msg_len)
{
    TCHAR slave_exec[32768];
    memset(slave_exec, NULL, sizeof(slave_exec));

    if (!get_self_path(slave_exec, ARRAYSIZE(slave_exec) - 128))
    {
        TRACE_ERROR("Unable to get path of slave executable, code = %d", GetLastError());
        return NULL;
    }
    append_platform_if_needed(slave_exec, params->slave_platform);
    _tcsncat(slave_exec, TEXT(".slave.exe"), ARRAYSIZE(slave_exec) - 1);
    if (GetFileAttributes(slave_exec) == INVALID_FILE_ATTRIBUTES)
    {
        TRACE_ERROR("Can't find slave executable.");
        return NULL;
    }

    if (_startup_mutex == NULL)
    {
        HANDLE new_mutex = CreateMutex(NULL, FALSE, NULL);
        if (!new_mutex)
        {
            TRACE_ERROR("CreateMutex failed, code = %d", GetLastError());
            return NULL;
        }
        if (InterlockedCompareExchangePointer(&_startup_mutex, new_mutex, NULL) != NULL)
        {
            // other thread wins
            CloseHandle(new_mutex);
        }
    }
    bool success = false;
    HANDLE pipe_stdin_read = NULL, pipe_stdin_write = NULL;
    HANDLE pipe_stdout_read = NULL, pipe_stdout_write = NULL;

    WaitForSingleObject(_startup_mutex, INFINITE);
    __try
    {
        CHECKED(SetEnvironmentVariableA, SCRIPT_VAR_NAME_A, params->script);
        SECURITY_ATTRIBUTES pipe_attrs;
        memset(&pipe_attrs, 0, sizeof(pipe_attrs));
        pipe_attrs.nLength = sizeof(pipe_attrs);
        pipe_attrs.bInheritHandle = TRUE;

        CHECKED(CreatePipe, &pipe_stdin_read, &pipe_stdin_write, &pipe_attrs, 0);
        CHECKED(SetHandleInformation, pipe_stdin_write, HANDLE_FLAG_INHERIT, 0);

        CHECKED(CreatePipe, &pipe_stdout_read, &pipe_stdout_write, &pipe_attrs, 0);
        CHECKED(SetHandleInformation, pipe_stdout_read, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFO si;
        PROCESS_INFORMATION pi;
        memset(&si, NULL, sizeof(si));
        memset(&pi, NULL, sizeof(pi));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.hStdInput = pipe_stdin_read;
        si.hStdError = pipe_stdout_write;
        si.hStdOutput = pipe_stdout_write;
        si.wShowWindow = SW_HIDE;
        // CREATE_BREAKAWAY_FROM_JOB is needed, because we need to 
        // place slave processes into our job, but sometimes
        // Windows will place the main process into a job, child 
        // processes will also be placed into the job without this
        // flag, and subsequent AssignProcessToJobObject will fail
        // because of that
        DWORD result;
        DWORD error_code = 0;
        result = CreateProcess(slave_exec, NULL, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, NULL, NULL, &si, &pi);
        if (!result) {
            error_code = GetLastError();
            if (error_code == ERROR_ACCESS_DENIED) {
                // Maybe the parent job doesn't have JOB_OBJECT_LIMIT_BREAKAWAY_OK
                CHECKED(CreateProcess, slave_exec, NULL, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
            } else {
                TRACE_ERROR("CreateProcess failed, code = %d", error_code);
                return NULL;
            }
        }

        CloseHandle(pi.hThread);
        CloseHandle(pipe_stdin_read);
        pipe_stdin_read = NULL;
        CloseHandle(pipe_stdout_write);
        pipe_stdout_write = NULL;

        assert(params->slave_job_object);
        result = AssignProcessToJobObject(params->slave_job_object, pi.hProcess);
        if (!result)
        {
            error_code = GetLastError();
        }
        CloseHandle(pi.hProcess);
        if (!result)
        {
            assert(false);
            TRACE("AssignProcessToJobObject failed, code = %d", error_code);
            // don't return, this won't cause any error, it is just that
            // slave processes won't be killed if the main process 
            // unexpectedly terminates
        }

        memset(error_msg, 0, error_msg_len);
        DWORD bytes_read = 0;
        CHECKED(ReadFile, pipe_stdout_read, error_msg, (DWORD)error_msg_len - 1, &bytes_read, NULL);
        if (memcmp(error_msg, SLAVE_OK_FLAG, strlen(SLAVE_OK_FLAG)) == 0)
        {
            success = true;
        } else {
            DWORD cur_bytes_read = 0;
            do 
            {
                if (ReadFile(pipe_stdout_read, error_msg + bytes_read, (DWORD)(error_msg_len - 1 - bytes_read), &cur_bytes_read, NULL))
                {
                    bytes_read += cur_bytes_read;
                } else {
                    break;
                }
            } while (cur_bytes_read > 0 && bytes_read < error_msg_len -1);
        }
        CloseHandle(pipe_stdout_read);
        pipe_stdout_read = NULL;
        if (success)
        {
            return pipe_stdin_write;
        } else {
            return NULL;
        }
    }
    __finally
    {
        ReleaseMutex(_startup_mutex);
        if (!success)
        {
            CloseHandle(pipe_stdin_read);
            CloseHandle(pipe_stdin_write);
            CloseHandle(pipe_stdout_read);
            CloseHandle(pipe_stdout_write);
        }
    }
}

#undef TRACE_ERROR
#undef CHECKED

void create_slave(IScriptEnvironment* env, slave_create_params* params, int* new_slave_port, HANDLE* slave_stdin_handle)
{
    *new_slave_port = 0;
    *slave_stdin_handle = NULL;

    size_t new_script_size = strlen(params->script) + 4096;
    char* new_script = new char[new_script_size];
    __try
    {
        memset(new_script, 0, new_script_size);
        {
            TCHAR self_path[32768];
            memset(self_path, 0, sizeof(self_path));
            if (!get_self_path(self_path, ARRAYSIZE(self_path)))
            {
                env->ThrowError("%s: Unable to get path of self.", params->filter_name);
            }
            append_platform_if_needed(self_path, params->slave_platform);
        }
        strcat(new_script, params->script);
        strcat(new_script, "\n");
        int port = get_unused_port();
        if (port <= 0)
        {
            env->ThrowError("%s: Unable to get unused port, code: %d", params->filter_name, port);
        }
        sprintf_append(new_script, "function %s() {\n return %d \n}\n", GET_OUTPUT_PORT_FUNCTION_NAME, port);

        char error_msg[1024];
        memset(error_msg, 0, sizeof(error_msg));

        slave_create_params new_params;
        memcpy(&new_params, params, sizeof(new_params));
        new_params.script = new_script;

        TRACE("Creating slave, script: %hs", new_script);

        *slave_stdin_handle = create_slave_process(&new_params, error_msg, sizeof(error_msg));
        if (!*slave_stdin_handle)
        {
            char error_buffer[1536];
            memset(error_buffer, 0, sizeof(error_buffer));
            sprintf(error_buffer, "%s: Unable to create slave process. Message: %s", params->filter_name, error_msg);
            env->ThrowError(error_buffer);
        }

        *new_slave_port = port;
    }
    __finally
    {
        delete[] new_script;
    }
}