#define UNICODE
#define _UNICODE

#include "common.h"

#include <Windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <atomic>
#include <mutex>

#include "slave_common.h"
#include "ScopeExit.h"

#include "splitprocess.h"

#define TRACE_COMPONENT L"slave_common"
#include "trace.h"

// one slave start at a time in the process: the environment variables passed to the slave
// and the inherited pipe handles are process wide
static std::mutex _startup_mutex;

static std::atomic<int> _port_counter(1);

int get_unused_port()
{
    return ++_port_counter;
}

#define TRACE_ERROR(format, ...) \
    do { \
        TRACE(format, ##__VA_ARGS__); \
        _snprintf(error_msg, error_msg_len, format, ##__VA_ARGS__); \
    } while (0)

#define CHECKED(name, ...) \
    do { \
        if (!name(__VA_ARGS__)) { \
            TRACE_ERROR("(slave_common.cpp) %hs failed, code = %lu", #name, GetLastError()); \
            return NULL; \
        } \
    } while (0)

HANDLE create_slave_process(slave_create_params* params, char* error_msg, size_t error_msg_len)
{
    TCHAR slave_exec[32768];
    memset(slave_exec, 0, sizeof(slave_exec));

    if (!get_self_path(slave_exec, ARRAYSIZE(slave_exec) - 128))
    {
        TRACE_ERROR("Unable to get path of slave executable, code = %lu", GetLastError());
        return NULL;
    }
    append_platform_if_needed(slave_exec, params->slave_platform);
    _tcsncat(slave_exec, TEXT(".slave.exe"), ARRAYSIZE(slave_exec) - 1);
    if (GetFileAttributes(slave_exec) == INVALID_FILE_ATTRIBUTES)
    {
        TRACE_ERROR("Can't find slave executable.");
        return NULL;
    }

    bool success = false;
    HANDLE pipe_stdin_read = NULL, pipe_stdin_write = NULL;
    HANDLE pipe_stdout_read = NULL, pipe_stdout_write = NULL;
    HANDLE slave_process = NULL;

    std::lock_guard<std::mutex> startup_lock(_startup_mutex);
    auto cleanup = make_scope_exit([&]
    {
        if (slave_process)
        {
            CloseHandle(slave_process);
        }
        if (!success)
        {
            CloseHandle(pipe_stdin_read);
            CloseHandle(pipe_stdin_write);
            CloseHandle(pipe_stdout_read);
            CloseHandle(pipe_stdout_write);
        }
    });
    {
        CHECKED(SetEnvironmentVariableA, SCRIPT_VAR_NAME_A, params->script);
        // NULL value removes the variable (failure is fine when it did not exist)
        SetEnvironmentVariableW(SCRIPT_NAME_VARS_VAR_NAME_W, params->script_name_vars);
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
        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
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
                TRACE_ERROR("CreateProcess failed, code = %lu", error_code);
                return NULL;
            }
        }

        CloseHandle(pi.hThread);
        CloseHandle(pipe_stdin_read);
        pipe_stdin_read = NULL;
        CloseHandle(pipe_stdout_write);
        pipe_stdout_write = NULL;

        assert(params->slave_job_object);
        slave_process = pi.hProcess; // kept until the end of the handshake, for its exit code
        result = AssignProcessToJobObject(params->slave_job_object, pi.hProcess);
        if (!result)
        {
            error_code = GetLastError();
        }
        if (!result)
        {
            assert(false);
            TRACE("AssignProcessToJobObject failed, code = %lu", error_code);
            // don't return, this won't cause any error, it is just that
            // slave processes won't be killed if the main process 
            // unexpectedly terminates
        }

        // Handshake: the slave prints SLAVE_OK_FLAG when its script is ready, or an error message
        // and exits. Other output may come first (e.g. a source filter printing its indexing progress
        // to stdout), so read until the flag shows up or the pipe is closed (slave exited).
        // A long output is kept in a sliding window: the latest output, where an error message is.
        memset(error_msg, 0, error_msg_len);
        const size_t SLAVE_OK_FLAG_LEN = strlen(SLAVE_OK_FLAG);
        const size_t capacity = error_msg_len - 1;
        size_t bytes_read = 0;
        while (true)
        {
            if (bytes_read >= capacity)
            {
                // keep the last 3/4 (a half received flag included), drop the oldest output
                const size_t keep = capacity * 3 / 4;
                memmove(error_msg, error_msg + bytes_read - keep, keep);
                memset(error_msg + keep, 0, error_msg_len - keep);
                bytes_read = keep;
            }
            DWORD cur_bytes_read = 0;
            if (!ReadFile(pipe_stdout_read, error_msg + bytes_read, (DWORD)(capacity - bytes_read), &cur_bytes_read, NULL) || cur_bytes_read == 0)
            {
                break; // pipe closed
            }
            bytes_read += cur_bytes_read;
            if (bytes_read >= SLAVE_OK_FLAG_LEN && strstr(error_msg, SLAVE_OK_FLAG) != nullptr)
            {
                success = true;
                break;
            }
        }
        if (!success)
        {
            // The slave reports its script error as the last thing it prints; drop preceding noise
            const char* last_error = nullptr;
            for (const char* p = strstr(error_msg, "Script error: "); p; p = strstr(p + 1, "Script error: "))
            {
                last_error = p;
            }
            if (last_error && last_error != error_msg)
            {
                memmove(error_msg, last_error, strlen(last_error) + 1);
            }
            size_t len = strlen(error_msg);
            while (len > 0 && (error_msg[len - 1] == '\r' || error_msg[len - 1] == '\n' || error_msg[len - 1] == ' '))
            {
                error_msg[--len] = 0;
            }
            if (len == 0)
            {
                // nothing printed: crashed or killed, the exit code is all we have
                DWORD exit_code = 0;
                WaitForSingleObject(slave_process, 5000);
                if (GetExitCodeProcess(slave_process, &exit_code) && exit_code != STILL_ACTIVE)
                {
                    _snprintf(error_msg, capacity, "The slave process exited without a message, exit code 0x%08lX.", exit_code);
                }
                else
                {
                    _snprintf(error_msg, capacity, "The slave process closed its output without a message.");
                }
            }
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
}

#undef TRACE_ERROR
#undef CHECKED

static bool get_string_var(IScriptEnvironment* env, const std::string& name, const char** value)
{
    try
    {
        AVSValue v = env->GetVar(name.c_str());
        if (v.IsString())
        {
            *value = v.AsString();
            return true;
        }
    }
    catch (IScriptEnvironment::NotFound) {}
    return false;
}

// Collects the script name variables of the current script (the one that called MP_Pipeline),
// see SCRIPT_NAME_VARS, as UTF-8 "Name=value\n" lines. Empty when none is set (e.g. no Import
// in the master).
static std::string collect_script_name_vars(IScriptEnvironment* env)
{
    std::string result;
    for (const char* name : SCRIPT_NAME_VARS)
    {
        const char* value = NULL;
        std::string value_utf8;
        if (get_string_var(env, std::string("$") + name + "Utf8$", &value))
        {
            value_utf8 = value;
        }
        else if (get_string_var(env, std::string("$") + name + "$", &value))
        {
            // AviSynth 2.6 or older AviSynth+: ANSI only
            value_utf8 = AnsiToUtf8(value);
        }
        if (!value_utf8.empty())
        {
            result += std::string(name) + "=" + value_utf8 + "\n";
        }
    }
    return result;
}

void create_slave(IScriptEnvironment* env, slave_create_params* params, int* new_slave_port, HANDLE* slave_stdin_handle)
{
    *new_slave_port = 0;
    *slave_stdin_handle = NULL;

    size_t new_script_size = strlen(params->script) + 4096;
    char* new_script = new char[new_script_size];
    auto cleanup = make_scope_exit([&] { delete[] new_script; });
    // the environment variable content (UTF-16), empty when there is nothing to pass
    const std::wstring script_name_vars = Utf8ToWideChar(collect_script_name_vars(env));
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

        char error_msg[4096];
        memset(error_msg, 0, sizeof(error_msg));

        slave_create_params new_params;
        memcpy(&new_params, params, sizeof(new_params));
        new_params.script = new_script;
        new_params.script_name_vars = script_name_vars.empty() ? NULL : script_name_vars.c_str();

        TRACE("Creating slave, script: %hs", new_script);

        *slave_stdin_handle = create_slave_process(&new_params, error_msg, sizeof(error_msg));
        if (!*slave_stdin_handle)
        {
            // error_msg is the slave's output, never use it as a format string
            env->ThrowError("%s: Unable to create slave process. Message: %s", params->filter_name, error_msg);
        }

        *new_slave_port = port;
    }
}