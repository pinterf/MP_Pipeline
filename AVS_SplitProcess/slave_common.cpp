#include "common.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <atomic>
#include <mutex>

#include "slave_common.h"
#include "ScopeExit.h"

#include "splitprocess.h"

#define TRACE_COMPONENT "slave_common"
#include "trace.h"

// one slave start at a time in the process: the environment variables passed to the slave
// and the inherited pipe handles are process wide
static std::mutex _startup_mutex;

static std::atomic<int> _port_counter(1);

int get_unused_port()
{
    return ++_port_counter;
}

// Starts the slave and waits for its handshake. Returns the slave's stdin (the slave runs while it is
// kept), null on failure with the reason in error_msg.
static std::unique_ptr<platform::ChildInput> create_slave_process(slave_create_params* params, char* error_msg, size_t error_msg_len)
{
    memset(error_msg, 0, error_msg_len);
    auto fail = [&](const char* message) {
        TRACE("%s", message);
        snprintf(error_msg, error_msg_len, "%s", message);
        return std::unique_ptr<platform::ChildInput>();
    };

    sys_string slave_exec = platform::module_path();
    if (slave_exec.empty())
    {
        return fail("Unable to get path of slave executable.");
    }
    const std::string suffix = platform_file_suffix(params->slave_platform) + ".slave.exe";
    slave_exec += sys_string(suffix.begin(), suffix.end()); // ASCII
    if (!platform::file_exists(slave_exec))
    {
        return fail("Can't find slave executable.");
    }

    // the environment variables and the inherited pipe handles are process wide
    std::lock_guard<std::mutex> startup_lock(_startup_mutex);
    if (!platform::set_environment_variable(SCRIPT_VAR_NAME_A, params->script))
    {
        return fail("Unable to pass the script to the slave process (environment).");
    }
    // NULL value removes the variable (failure is fine when it did not exist)
    platform::set_environment_variable(SCRIPT_NAME_VARS_VAR_NAME, params->script_name_vars);

    // closes our handles of the process and its output when leaving; the stdin is handed over
    // only on success, otherwise it is closed too and the slave sees the end of its input
    platform::ChildProcess slave;
    std::string start_error;
    if (!slave.start(slave_exec, *params->slave_group, start_error))
    {
        return fail(start_error.c_str());
    }

    // Handshake: the slave prints SLAVE_OK_FLAG when its script is ready, or an error message
    // and exits. Other output may come first (e.g. a source filter printing its indexing progress
    // to stdout), so read until the flag shows up or the pipe is closed (slave exited).
    // A long output is kept in a sliding window: the latest output, where an error message is.
    bool success = false;
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
        const size_t cur_bytes_read = slave.read_output(error_msg + bytes_read, capacity - bytes_read);
        if (cur_bytes_read == 0)
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
    if (success)
    {
        return slave.take_input();
    }

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
        uint32_t exit_code = 0;
        if (slave.wait_for_exit(5000, exit_code))
        {
            snprintf(error_msg, capacity, "The slave process exited without a message, exit code 0x%08lX.", (unsigned long)exit_code);
        }
        else
        {
            snprintf(error_msg, capacity, "The slave process closed its output without a message.");
        }
    }
    return std::unique_ptr<platform::ChildInput>();
}
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
            value_utf8 = platform::ansi_to_utf8(value);
        }
        if (!value_utf8.empty())
        {
            result += std::string(name) + "=" + value_utf8 + "\n";
        }
    }
    return result;
}

void create_slave(IScriptEnvironment* env, slave_create_params* params, int* new_slave_port, std::unique_ptr<platform::ChildInput>& slave_input)
{
    *new_slave_port = 0;
    slave_input.reset();

    size_t new_script_size = strlen(params->script) + 4096;
    char* new_script = new char[new_script_size];
    auto cleanup = make_scope_exit([&] { delete[] new_script; });
    // the environment variable content, empty when there is nothing to pass
    const sys_string script_name_vars = platform::utf8_to_sys(collect_script_name_vars(env));
    {
        memset(new_script, 0, new_script_size);

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

        TRACE("Creating slave, script: %s", new_script);

        slave_input = create_slave_process(&new_params, error_msg, sizeof(error_msg));
        if (!slave_input)
        {
            // error_msg is the slave's output, never use it as a format string
            env->ThrowError("%s: Unable to create slave process. Message: %s", params->filter_name, error_msg);
        }

        *new_slave_port = port;
    }
}