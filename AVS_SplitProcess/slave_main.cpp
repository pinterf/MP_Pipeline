// AVS_SplitProcess.cpp : Defines the entry point for the console application.
//

#include "common.h"
#include <string.h>
#include "avisynth.h"
#include "splitprocess.h"
#include "platform.h"
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <assert.h>
#include <stdlib.h>

#include "../AVS_SplitProcess/statement.h"

typedef IScriptEnvironment* (__stdcall *CreateScriptEnvironment_t)(int);

#define DLL_STATEMENT_START "^\\s*### dll:"
#define DLL_STATEMENT_PARAM "\\s*(.*?)\\s*$"

#define DISABLE_EXIT_HACK_STATEMENT "^\\s*### disable exit hack"

// Sets the master's script name variables (see SCRIPT_NAME_VARS), so ScriptDir(), ScriptName()
// etc. return the same as in the master's script.
static void set_script_name_vars(IScriptEnvironment* env)
{
    sys_string all_sys;
    if (!platform::get_environment_variable(SCRIPT_NAME_VARS_VAR_NAME, all_sys))
    {
        return;
    }
    const std::string all = platform::sys_to_utf8(all_sys);
    size_t pos = 0;
    while (pos < all.size())
    {
        size_t end = all.find('\n', pos);
        if (end == std::string::npos)
        {
            end = all.size();
        }
        std::string line = all.substr(pos, end - pos);
        pos = end + 1;
        size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }
        std::string name = line.substr(0, eq);
        std::string value = line.substr(eq + 1); // UTF-8
        bool known = false;
        for (const char* n : SCRIPT_NAME_VARS)
        {
            known = known || name == n;
        }
        if (!known || value.empty())
        {
            continue;
        }
        std::string ansi_name = "$" + name + "$";
        std::string utf8_name = "$" + name + "Utf8$";
        env->SetGlobalVar(env->SaveString(ansi_name.c_str()), env->SaveString(platform::utf8_to_ansi(value).c_str()));
        env->SetGlobalVar(env->SaveString(utf8_name.c_str()), env->SaveString(value.c_str()));
    }
}

#ifndef _DEBUG
// the slave does not always exit (filters failing in their destructors): the last resort
static void exit_on_timeout()
{
    std::this_thread::sleep_for(std::chrono::seconds(15));
    platform::exit_process(253);
}
#endif

int main()
{
    // the handshake and error messages for the master; filters' own output goes to stderr
    FILE* stdout_file = platform::take_over_stdout();
    if (!stdout_file)
    {
        // nothing to report the problem to: the master sees the exit code
        return 8;
    }
    platform::exit_when_master_ends();
    std::string script_text;
    if (!platform::get_environment_variable(SCRIPT_VAR_NAME_A, script_text))
    {
      fprintf(stdout_file, "Unable to read the script from the environment (%s).\n", SCRIPT_VAR_NAME_A);
        return 1;
    }
    const char* script = script_text.c_str();
    if (strlen(script) == 0)
    {
      fprintf(stdout_file, "Script is empty.\n");
        return 2;
    }
    std::string input_error;
    if (!platform::check_input(input_error))
    {
        fprintf(stdout_file, "%s\n", input_error.c_str());
        return 5;
    }
    const char* avisynth_module_name = platform::default_avisynth_library();
    scan_statement(script, DLL_STATEMENT_START DLL_STATEMENT_PARAM, NULL,
        NULL, &avisynth_module_name,
        NULL, NULL
    );

    std::string load_error;
    void* avisynth_module = platform::load_library(avisynth_module_name, load_error);
    if (!avisynth_module)
    {
        fprintf(stdout_file, "Unable to load %s: %s\n", avisynth_module_name, load_error.c_str());
        return 9;
    }
    CreateScriptEnvironment_t create_script_env = (CreateScriptEnvironment_t)platform::library_symbol(avisynth_module, "CreateScriptEnvironment");
    if (!create_script_env)
    {
        fprintf(stdout_file, "%s doesn't contain entry point of CreateScriptEnvironment\n", avisynth_module_name);
        return 10;
    }

    // Minimum required version: 6 (AviSynth 2.6 and any AviSynth+).
    // For a C++ host AviSynth+ returns the same full environment for any request between 6 and its
    // current version, so asking for 6 does not disable V8+ frame properties or V11+ 64 bit
    // script types (long/double). Only 2.5 C++ hosts (<=3) and C API hosts (<11) get a narrowed env.
    const int ASK_FOR_INTERFACE_VERSION = AVISYNTH_CLASSIC_INTERFACE_VERSION;
    IScriptEnvironment* env = create_script_env(ASK_FOR_INTERFACE_VERSION);
    {
        AVS_linkage = env->GetAVSLinkage();

        AVSValue v;
        AVSValue script_value = script;
        try
        {
          set_script_name_vars(env);
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
        // the master closes our stdin when the pipeline is done
        if (!platform::wait_for_end_of_input(input_error))
        {
            fprintf(stdout_file, "%s\n", input_error.c_str());
        }
        fprintf(stdout_file, "Terminating...\n");
        fflush(stdout_file);

#ifndef _DEBUG
        // Sometimes filters have problem on destruction, try to silent them
        if (!has_statement(script, DISABLE_EXIT_HACK_STATEMENT)) {
            platform::suppress_error_dialogs();
        }

        // Not sure why slave processes do not exit sometimes... here is another hack
        std::thread(exit_on_timeout).detach();
#endif
    }
    env->DeleteScriptEnvironment();

    // the AviSynth library is not unloaded, to prevent problems: the process ends anyway
    platform::exit_process(0);
}

// 2.6+ interface: this one should be filled manually after getting env
const AVS_Linkage* AVS_linkage = nullptr;

