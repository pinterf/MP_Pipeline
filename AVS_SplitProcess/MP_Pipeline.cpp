#define MP_PIPELINE_IMPLEMENTATION

#include "stdafx.h"
#include "MP_Pipeline.h"
#include "slave_common.h"
#include "statement.h"
#include "utils.h"
#include "trace.h"

#include <regex>
#include <sstream>
#include <string>
#include <functional>

// For UUID functions
#include <rpc.h>
#include "rpcdce.h"
#pragma comment(lib, "rpcrt4.lib")

#define BRANCH_STATEMENT_START "^\\s*### branch:"
#define BRANCH_STATEMENT_PARAM "\\s*(\\d+)(?:\\s*,\\s*(\\d{1,7}))?\\s*$"

#define PLATFORM_SCAN_FORMAT "%16s"
#define PLATFORM_PATTERN "^\\s*### platform:\\s*(\\w+)\\s*$"

#define PREFETCH_STATEMENT_START "^\\s*### prefetch:"
#define PREFETCH_STATEMENT_PARAM "\\s*(\\d+)\\s*,\\s*(\\d+)\\s*$"

#define EXPORT_CLIP_STATEMENT_START "^\\s*### export clip:"
#define PASS_CLIP_STATEMENT_START "^\\s*### pass clip:"
#define CLIP_LIST_STATEMENT_PARAM "\\s*((?:[_A-Za-z][_A-Za-z0-9]*\\s*(?:,\\s*|$))+)"

#define TAG_INHERIT_START "### inherit start ###"
#define TAG_INHERIT_END "### inherit end ###"

#define MT_MODE_START "^\\s*### setmtmode:"
#define MT_MODE_PARAM "\\s*(\\d+)\\s*,\\s*(\\d+)\\s*$"

#define LOCK_THREADS_TO_CORES_START "^\\s*### lock threads to cores"
#define LOCK_THREADS_TO_CORES_PARAM "\\s*(\\d+)\\s*$"

#define LOAD_PLUGIN_FUNCTION_NAME "MPP_Load"
#define GET_UPSTREAM_CLIP_FUNCTION_NAME "MPP_GetUpstreamClip"
#define PREPARE_DOWNSTREAM_CLIP_FUNCTION_NAME "MPP_PrepareDownstreamClip"
#define CLIENT_TEMPLATE "MPP_SharedMemoryClient(\"%s\", %d, \"None\", clip_index=clip_index)"

static const int DEFAULT_THUNK_SIZE = 1;


using namespace std;

AVSValue __cdecl Create_MP_Pipeline(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    return new MP_Pipeline(MP_Pipeline_parameter_storage_t(args), env);
}

MP_Pipeline::MP_Pipeline(const MP_Pipeline_parameter_storage_t& o, IScriptEnvironment* env)
    : MP_Pipeline_parameter_storage_t(o)
{
    memset(_instance_id, 0, sizeof(_instance_id));
    UUID uuid;
    UuidCreate(&uuid);
    RPC_CSTR uuid_str = NULL;
    UuidToStringA(&uuid, &uuid_str);
    strncpy(_instance_id, (const char*)uuid_str, ARRAYSIZE(_instance_id) - 1);
    RpcStringFreeA(&uuid_str);

    memset(&_slave_stdin_handles, NULL, sizeof(_slave_stdin_handles));

    // the job object is used to make the system kill all slave processes if the main process is unexpectedly terminated
    _slave_job = CreateJobObject(NULL, NULL);
    if (!_slave_job)
    {
        env->ThrowError("MP_Pipeline: Unable to create job object, code = %d", GetLastError());
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_info;
    memset(&limit_info, 0, sizeof(limit_info));
    limit_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
    if (!SetInformationJobObject(_slave_job, JobObjectExtendedLimitInformation, &limit_info, sizeof(limit_info)))
    {
        env->ThrowError("MP_Pipeline: SetInformationJobObject failed, code = %d", GetLastError());
    }
    this->create_pipeline(env);
}

void MP_Pipeline::close_slave_stdin_handles()
{
    for (int i = 0; i < MAX_SLAVES; i++)
    {
        if (!_slave_stdin_handles[i])
        {
            break;
        }
        CloseHandle(_slave_stdin_handles[i]);
        _slave_stdin_handles[i] = NULL;
    }
}

MP_Pipeline::~MP_Pipeline()
{
    close_slave_stdin_handles();
    if (_slave_job)
    {

        if (!_always_kill_children) {
            // since we are exiting normally, reset the job limitation to default,
            // so that the slave processes can clean up and terminate themselves,
            // and the main process don't need to wait for them
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_info;
            memset(&limit_info, 0, sizeof(limit_info));
            SetInformationJobObject(_slave_job, JobObjectExtendedLimitInformation, &limit_info, sizeof(limit_info));
        }
        CloseHandle(_slave_job);
        _slave_job = NULL;
    }
}

void add_load_plugin_function(char* script, const char* platform, IScriptEnvironment* env)
{
    char self_path[32768];
    if (!get_self_path(self_path, ARRAYSIZE(self_path) - 32))
    {
        env->ThrowError("get_self_path failed, code = %d", GetLastError());
    }
    append_platform_if_needed(self_path, platform);
    sprintf_append(script, "function %s() {\n LoadPlugin(\"%s\")\n }\n", LOAD_PLUGIN_FUNCTION_NAME, self_path);
}

bool process_clip_list_statement(const char* statement_start, const char* statement_full, const char* script, function<void (const char*)> callback)
{
    char* statement_param;
    if (!has_statement(script, statement_start))
    {
        return true;
    }
    if (!scan_statement(script, statement_full, NULL, 
        NULL, &statement_param,
        NULL, NULL))
    {
        return false;
    }
    const char* current_pos = statement_param;
    regex re("^\\s*([_A-Za-z0-9]+)\\s*(?:,|$)", regex::ECMAScript);
    cmatch m;
    while (*current_pos)
    {
        if (!regex_search(current_pos, m, re))
        {
            free(statement_param);
            return false;
        }
        callback(m[1].str().c_str());
        current_pos = m[0].second;
    }
    free(statement_param);
    return true;
}

bool process_export_clip_statement(const char* script, function<void (const char*)> callback)
{
    return process_clip_list_statement(EXPORT_CLIP_STATEMENT_START, EXPORT_CLIP_STATEMENT_START CLIP_LIST_STATEMENT_PARAM, script, callback);
}

void append_import_clip_signature(char* s, const char* clip_var_name)
{
    sprintf_append(s, "### imported clip: %s ###", clip_var_name);
}

void process_pass_clip_statement(char* script, char* next_script, IScriptEnvironment* env)
{
    bool result = process_clip_list_statement(PASS_CLIP_STATEMENT_START, PASS_CLIP_STATEMENT_START CLIP_LIST_STATEMENT_PARAM, script,
        [=] (const char* clip_var_name) {
            char* pattern = (char*)malloc(strlen(clip_var_name) + 256);
            strcpy(pattern, "^.*");
            append_import_clip_signature(pattern, clip_var_name);
            strcat(pattern, "$");
            regex re(pattern, regex::ECMAScript);
            free(pattern);
            pattern = NULL;

            cmatch m;
            if (regex_search(script, m, re))
            {
                strcat(next_script, m[0].str().c_str());
                strcat(next_script, "\n");
            } else {
                env->ThrowError("MP_Pipeline: %s is not an imported clip.", clip_var_name);
            }
        }
    );    
    if (!result)
    {
        env->ThrowError("MP_Pipeline: Invalid pass clip statement.");
    }
}

void prepare_export_clip(char* script, char* next_script, int process_id, IScriptEnvironment* env)
{
    int i = 0;
    bool result = process_export_clip_statement(script, [=, &i] (const char* clip_var_name) {
        i++;
        sprintf_append(script, "%s = %s.%s()\n", clip_var_name, clip_var_name, PREPARE_DOWNSTREAM_CLIP_FUNCTION_NAME);
        sprintf_append(next_script, "%s = %s_%d(%d) ", clip_var_name, GET_UPSTREAM_CLIP_FUNCTION_NAME, process_id, i);
        append_import_clip_signature(next_script, clip_var_name);
        strcat(next_script, "\n");
    });
    if (!result)
    {
        env->ThrowError("MP_Pipeline: Invalid export statement");
    }
}

void MP_Pipeline::prepare_slave(slave_create_params* params, cpu_arrangement_info_t* cpu_arrangement_info, IScriptEnvironment* env)
{
    params->slave_job_object = _slave_job;
    scan_statement(params->script, PLATFORM_PATTERN, NULL, PLATFORM_SCAN_FORMAT, params->slave_platform);

    sprintf_append(params->script, "MPP_SharedMemoryServer(\"%s\", %s()", _instance_id, GET_OUTPUT_PORT_FUNCTION_NAME);

    auto export_callback = [params] (const char* clip_var_name) {
        sprintf_append(params->script, ", %s", clip_var_name);
    };

    if (!process_export_clip_statement(params->script, export_callback))
    {
        env->ThrowError("MP_Pipeline: Unexpected error: process_export_clip_statement in prepare_slave failed.");
    }

    if (has_statement(params->script, PREFETCH_STATEMENT_START))
    {
        int max_cache_frames, cache_behind;
        if (!scan_statement(params->script, PREFETCH_STATEMENT_START PREFETCH_STATEMENT_PARAM, NULL, 
            "%d", &max_cache_frames,
            "%d", &cache_behind,
            NULL, NULL))
        {
            env->ThrowError("MP_Pipeline: Invalid cache statement.");
        }
        sprintf_append(params->script, ", max_cache_frames=%d, cache_behind=%d", max_cache_frames, cache_behind);
    }
    if (has_statement(params->script, LOCK_THREADS_TO_CORES_START) && cpu_arrangement_info->cpu_count > 1)
    {
        int new_index = 0;
        if (scan_statement(params->script, LOCK_THREADS_TO_CORES_START LOCK_THREADS_TO_CORES_PARAM, NULL,
            "%d", &new_index))
            cpu_arrangement_info->current_index = new_index;
        int fetcher_cpu_index = cpu_arrangement_info->arrangement[cpu_arrangement_info->current_index];
        cpu_arrangement_info->current_index++;
        cpu_arrangement_info->current_index %= cpu_arrangement_info->cpu_count;


        int server_cpu_index = cpu_arrangement_info->arrangement[cpu_arrangement_info->current_index];
        sprintf_append(params->script, ", fetcher_thread_lock_to_cpu=%d, server_thread_lock_to_cpu=%d", 
            fetcher_cpu_index, server_cpu_index);
    }
    strcat(params->script, ")\n");

    add_load_plugin_function(params->script, params->slave_platform, env);
}

void copy_inherit_block(const char* source, char* target)
{
    std::regex start_pattern("\\s*" + std::string(TAG_INHERIT_START) + "\\s*");
    std::regex end_pattern("\\s*" + std::string(TAG_INHERIT_END) + "\\s*");

    std::stringstream section;
    std::string line;

    std::stringstream in(source);
    bool inside = false;
    while (std::getline(in, line, '\n')) {
      if (inside) {
        section << line << std::endl;
        if (std::regex_match(line, end_pattern))
        {
          strcat(target, section.str().c_str());
          section = std::stringstream(); // reset stream
          inside = false;
        }
      }
      else {
        if (std::regex_match(line, start_pattern))
        {
          section << line << std::endl;
          inside = true;
        }
      }
    } 
/*
  // old regex version kept for reference: 
  // When character count between sections is exceeding some hundred characters, it becomes too complex
  // and results in exception "error_stack", e.g. stack is limited to e.g. 600 steps
  /6 diring regex evaluation

  char pattern[256];
  sprintf(pattern, "^\\s*%s\\s*$(?:.|\\s)*?^\\s*%s\\s*$", TAG_INHERIT_START, TAG_INHERIT_END);
  // ^\s*### inherit start ###\s*$(?:.|\s)*?^\s*### inherit end ###\s*$
  regex re(pattern, regex::ECMAScript);
  cmatch m;

  try {
    // copy (optionally multiple) inherit start-end sections
    while (regex_search(source, m, re))
    {
      strncat(target, m[0].first, m[0].length()); // first start section-end section, inclusive start-end tags
      strcat(target, "\n");
      source = m[0].second; // pointer to after the first end section
    }
  }
  catch (const std::regex_error& e)
  {
    if (e.code() == std::regex_constants::error_complexity)
      throw AvisynthError("MP_Pipeline: error in regex parsing 'inherit block': The complexity of an attempted match against a regular expression exceeded a pre-set level.");
    else if (e.code() == std::regex_constants::error_stack)
      throw AvisynthError("MP_Pipeline: error in regex parsing 'inherit block': There was insufficient memory to determine whether the regular expression could match the specified character sequence.");
    std::string err = "MP_Pipeline: Error in regex library while copying 'inherit block':\n" + std::string(e.what())
      + "\nCode:" + std::string(std::to_string(e.code()));
    throw AvisynthError(err.c_str());
  }
*/
    // Ensure last is a clip even after LoadPlugin() calls
    strcat(target, "\nlast\n");
}

void begin_get_upstream_clip_function(char* script, int process_id)
{
    sprintf_append(script, "%s\nfunction %s_%d(int clip_index) {\n", TAG_INHERIT_START, GET_UPSTREAM_CLIP_FUNCTION_NAME, process_id);
}

void end_get_upstream_clip_function(char* script)
{
    sprintf_append(script, "}\n%s\n", TAG_INHERIT_END);
}

void begin_prepare_downstream_clip_function(char* script)
{
    sprintf_append(script, "function %s(clip c) {\n", PREPARE_DOWNSTREAM_CLIP_FUNCTION_NAME);
}

void end_prepare_downstream_clip_function(char* script)
{
    strcat(script, "}\n");
}

void MP_Pipeline::create_branch(char* script, char* next_script, int* slave_count, cpu_arrangement_info_t* cpu_arrangement_info, IScriptEnvironment* env)
{
    int branch_count = 0;
    char* thunk_size_str = NULL;
    char* branch_line_ptr = NULL;
    int thunk_size = DEFAULT_THUNK_SIZE;

    if (!scan_statement(script, BRANCH_STATEMENT_START BRANCH_STATEMENT_PARAM, &branch_line_ptr, "%d", &branch_count, NULL, &thunk_size_str, NULL, NULL))
    {
        env->ThrowError("MP_Pipeline: Invalid branch statement.");
    }
    if (thunk_size_str && strlen(thunk_size_str) > 0)
    {
        sscanf(thunk_size_str, "%d", &thunk_size);
    }
    // free(thunk_size_str);
    if (branch_count < 2 || branch_count >= MAX_SLAVES)
    {
        env->ThrowError("MP_Pipeline: Invalid branch count: %d", branch_count);
    }
    if (thunk_size <= 0)
    {
        env->ThrowError("MP_Pipeline: Invalid thunk size: %d", thunk_size);
    }
    *branch_line_ptr = NULL;
    size_t buffer_size = strlen(script) + 1024;
    int process_id = *slave_count;
    char* buffer = (char*)malloc(buffer_size);
    __try
    {
        memset(buffer, 0, buffer_size);

        const char* GET_BRANCH_ID = "MPP_GetBranchID";
        _snprintf(buffer, buffer_size, "global MP_PIPELINE_BRANCH_ID = %s()\n%s\n%s()\n", GET_BRANCH_ID, script, PREPARE_DOWNSTREAM_CLIP_FUNCTION_NAME);
        prepare_export_clip(buffer, next_script, process_id, env);
        sprintf_append(buffer, "%s\n", branch_line_ptr + 1);

        char* script_end = buffer + strlen(buffer);

        begin_get_upstream_clip_function(next_script, process_id);
        sprintf_append(next_script, "return ThunkedInterleave(%d,", thunk_size);

        for (int i = 0; i < branch_count; i++)
        {
            if ((*slave_count) >= MAX_SLAVES)
            {
                env->ThrowError("MP_Pipeline: Too many slaves.");
            }
            *script_end = NULL;

            sprintf_append(buffer, "function %s() {\n return %d \n}\n", GET_BRANCH_ID, i);

            begin_prepare_downstream_clip_function(buffer);
            sprintf_append(buffer, "return c.SelectThunkEvery(%d, %d, %d)\n", thunk_size, branch_count, i);
            end_prepare_downstream_clip_function(buffer);
            
            slave_create_params params;

            memset(&params, 0, sizeof(params));
            params.filter_name = "MP_Pipeline";
            params.script = buffer;
            prepare_slave(&params, cpu_arrangement_info, env);

            int port = -1;
            create_slave(env, &params, &port, _slave_stdin_handles + *slave_count);

            sprintf_append(next_script, "%s" CLIENT_TEMPLATE, i == 0 ? "" : ", ", _instance_id, port);

            (*slave_count)++;
        }
        strcat(next_script, ")\n");
        end_get_upstream_clip_function(next_script);
    }
    __finally
    {
        free(buffer);
    }
}

// workaround for C2712
void MP_Pipeline::create_pipeline_finish(char* script, IScriptEnvironment* env)
{
    try
    {
        AVSValue value = env->Invoke("Eval", script);
        if (!value.IsClip())
        {
            throw AvisynthError("Not a clip.");
        }
        SetChild(value.AsClip());
    }
    catch (AvisynthError& e)
    {
        env->ThrowError("MP_Pipeline: Error while creating last part of the filter chain:\n%s", e.msg);
    }
}

template <typename T>
int count_bits_set(T number)
{
    int result = 0;
    for (int i = 0; i < sizeof(number) * 8; i++)
    {
        if ((number & 1) == 1)
        {
            result++;
        }
        number >>= 1;
    }
    return result;
}

int get_cpu_arrangement(int* arrangement)
{
    ULONG_PTR cpu_groups[MAX_CPU];
    memset(cpu_groups, 0, sizeof(cpu_groups));

    int cpu_group_count = 0;
    int logical_processor_count = 0;

    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
    __try
    {
        DWORD buffer_size = 0;

        while (true)
        {
            BOOL result = GetLogicalProcessorInformation(buffer, &buffer_size);
            if (result)
            {
                break;
            }
            free(buffer);
            buffer = NULL;

            DWORD code = GetLastError();
            if (code != ERROR_INSUFFICIENT_BUFFER)
            {
                TRACE("GetLogicalProcessorInformation failed, code = %d", code);
                return 0;
            }
            buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(buffer_size);
            if (!buffer)
            {
                TRACE("Unable to allocate buffer for GetLogicalProcessorInformation.");
                return 0;
            }
        }
        auto ptr = buffer;
        while (buffer_size >= sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) && cpu_group_count < MAX_CPU)
        {
            if (ptr->Relationship == RelationProcessorCore)
            {
                cpu_groups[cpu_group_count] = ptr->ProcessorMask;
                cpu_group_count++;
                logical_processor_count += count_bits_set(ptr->ProcessorMask);
            }
            ptr++;
            buffer_size -= sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); 
        }

    } __finally {
        free(buffer);
        buffer = NULL;
    }

    if (logical_processor_count <= 0)
    {
        // will this happen?
        assert(false);
        return 0;
    }

    DWORD_PTR dummy = 0, system_affinity_mask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &dummy, &system_affinity_mask))
    {
        TRACE("GetProcessAffinityMask failed, code = %d", GetLastError());
        return 0;
    }
    int usable_processor_count = count_bits_set(system_affinity_mask);
    if (usable_processor_count > logical_processor_count)
    {
        assert(false);
        return 0;
    }

    int current_group = 0;
    // fill the arrangement array, ensure different processes are spreaded over all available cores, aware of hyper-threading
    for (int lp_i = 0; lp_i < min(usable_processor_count, MAX_CPU); lp_i++)
    {
        while (true)
        {
            bool found_cpu = false;
            for (int c_i = 0; c_i < MAX_CPU; c_i++)
            {
                ULONG_PTR mask = ((ULONG_PTR)1) << c_i;
                if ((cpu_groups[current_group] & mask) == mask)
                {
                    cpu_groups[current_group] &= ~mask;
                    if ((system_affinity_mask & mask) == 0)
                    {
                        // we can't use this processor
                        continue;
                    }
                    found_cpu = true;
                    *arrangement = c_i;
                    arrangement++;
                    break;
                }
            }
            current_group++;
            if (current_group >= cpu_group_count)
            {
                current_group = 0;
            }
            if (found_cpu)
            {
                break;
            }
        }

    }
    return logical_processor_count;
}

void MP_Pipeline::create_pipeline(IScriptEnvironment* env)
{
    char* script_dup = _strdup(_script);
    char* buffer_store = NULL;
    char* current_script_part = NULL;
    char* next_script_part = NULL;
    int branch_ports[MAX_SLAVES + 1];
    memset(branch_ports, NULL, sizeof(branch_ports));
    bool completed = false;

    cpu_arrangement_info_t cpu_arrangement_info;
    memset(&cpu_arrangement_info, 0xFF, sizeof(cpu_arrangement_info));
    cpu_arrangement_info.cpu_count = get_cpu_arrangement(cpu_arrangement_info.arrangement);
    assert(cpu_arrangement_info.arrangement[MAX_CPU] == -1);
    assert(cpu_arrangement_info.cpu_count <= MAX_CPU);
    cpu_arrangement_info.current_index = 0;

    __try
    {
        size_t buffer_size = strlen(_script) + 256000;
        buffer_store = (char*)malloc(buffer_size * 2);
        memset(buffer_store, 0, buffer_size * 2);
        current_script_part = buffer_store;
        next_script_part = buffer_store + buffer_size;
        char* current_pos = script_dup;
        int slave_count = 0;
        int port = 0;

        sprintf(current_script_part, "%s()\n", LOAD_PLUGIN_FUNCTION_NAME);
        while (true)
        {
            sprintf(next_script_part, "%s()\n", LOAD_PLUGIN_FUNCTION_NAME);

            int process_id = slave_count;
            sprintf_append(next_script_part, "%s_%d(0)\n", GET_UPSTREAM_CLIP_FUNCTION_NAME, process_id);

            char* splitter_pos = NULL;
            char* splitter_line = NULL;
            if (!scan_statement(current_pos, SCRIPT_SPLITTER_PATTERN, &splitter_pos, NULL, &splitter_line))
            {
                break;
            }
            // there may be spaces in the splitter line, so we must get its length in runtime
            size_t splitter_length = strlen(splitter_line);
            free(splitter_line);
            splitter_line = NULL;
            if (slave_count >= MAX_SLAVES)
            {
                env->ThrowError("MP_Pipeline: Too many slaves");
            }
            // terminate current script part
            memset(splitter_pos, 0, splitter_length);

            strcat(current_script_part, current_pos);
            copy_inherit_block(current_script_part, next_script_part);

            process_pass_clip_statement(current_script_part, next_script_part, env);

            if (has_statement(current_script_part, BRANCH_STATEMENT_START))
            {
                create_branch(current_script_part, next_script_part, &slave_count, &cpu_arrangement_info, env);
                port = 0;
            } else {
                sprintf_append(current_script_part, "%s()\n", PREPARE_DOWNSTREAM_CLIP_FUNCTION_NAME);
                prepare_export_clip(current_script_part, next_script_part, process_id, env);

                begin_prepare_downstream_clip_function(current_script_part);
                strcat(current_script_part, "return c\n");
                end_prepare_downstream_clip_function(current_script_part);

                slave_create_params params;
                memset(&params, 0, sizeof(params));
                params.filter_name = "MP_Pipeline";
                params.script = current_script_part;
                prepare_slave(&params, &cpu_arrangement_info, env);

                create_slave(env, &params, &port, _slave_stdin_handles + slave_count);
				
			int mt_mode, mt_threads;
			bool mt_mode_b = false;
			if (has_statement(current_script_part, MT_MODE_START))
			{
				if (!scan_statement(current_script_part, MT_MODE_START MT_MODE_PARAM, NULL, 
					"%d", &mt_mode,
					"%d", &mt_threads,
					NULL, NULL))
				{
					env->ThrowError("MP_Pipeline: Invalid SetMTMode statement.");
				} else {
					mt_mode_b = true;
				}
			}

                begin_get_upstream_clip_function(next_script_part, process_id);
				if (mt_mode_b)
				{
					sprintf_append(next_script_part, "SetMTMode(%d,%d)\nreturn " CLIENT_TEMPLATE "\n", mt_mode, mt_threads, _instance_id, port);
				} else {
                sprintf_append(next_script_part, "return " CLIENT_TEMPLATE "\n", _instance_id, port);
				}
                end_get_upstream_clip_function(next_script_part);
            }
            current_pos = splitter_pos + splitter_length;

            char* swap_tmp = current_script_part;
            current_script_part = next_script_part;
            next_script_part = swap_tmp;

            slave_count++;
        }
        if (slave_count == 0)
        {
            env->ThrowError("MP_Pipeline: Can't find any splitter.");
        }
        strcat(current_script_part, current_pos);
        sprintf_append(current_script_part, "function %s() {}\n", LOAD_PLUGIN_FUNCTION_NAME);
        create_pipeline_finish(current_script_part, env);
        completed = true;
    }
    __finally
    {
        if (!completed) {
            // Some error happened, terminate all slave processes
            CloseHandle(_slave_job);
            _slave_job = NULL;
            close_slave_stdin_handles();
        }
        free(buffer_store);
        free(script_dup);
    }
}