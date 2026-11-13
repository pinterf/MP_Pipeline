#pragma once

#include "MP_Pipeline.def.h"
#include "GenericVideoFilter2.h"
#include "slave_common.h"

static const char* SCRIPT_SPLITTER_PATTERN = "^(\\s*### ###\\s*)$";



class MP_Pipeline : public GenericVideoFilter2, MP_Pipeline_parameter_storage_t
{
public:
    MP_Pipeline(const MP_Pipeline_parameter_storage_t& o, IScriptEnvironment* env);
    virtual ~MP_Pipeline();
private:
    void create_pipeline(IScriptEnvironment* env);

    void create_branch(char* script, char* next_script, int* slave_count, int block_number, IScriptEnvironment* env);
    void create_pipeline_finish(char* script, IScriptEnvironment* env);

    void prepare_slave(slave_create_params* params, IScriptEnvironment* env);
    void close_slave_stdin_handles();

    char _instance_id[100];
    HANDLE _slave_stdin_handles[MAX_SLAVES + 1];
    HANDLE _slave_job;
};
