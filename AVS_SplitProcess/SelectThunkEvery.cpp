#include "common.h"
#define SELECTTHUNKEVERY_IMPLEMENTATION
#define SELECTTHUNKEVERY_SIMPLE_MACRO_NAME
#include "SelectThunkEvery.h"
#include <assert.h>

AVSValue __cdecl Create_SelectThunkEvery(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    return new SelectThunkEvery(ARG(child).AsClip(), SelectThunkEvery_parameter_storage_t(args), env);
}

int roundup_div(int a, int b)
{
    assert(a >= 0);
    assert(b > 0);
    int result = a / b;
    if (a % b != 0)
    {
        result++;
    }
    return result;
}


SelectThunkEvery::SelectThunkEvery(PClip child, const SelectThunkEvery_parameter_storage_t& o, IScriptEnvironment* env) :
GenericVideoFilter(child),
    SelectThunkEvery_parameter_storage_t(o)
{
    if (_thunk_size <= 0)
    {
        env->ThrowError("SelectThunkEvery: thunk_size must be greater than 0.");
    }
    int total_thunk_count = roundup_div(vi.num_frames, _thunk_size);
    if (_every <= 0)
    {
        env->ThrowError("SelectThunkEvery: must be greater than 0.");
    }
    if (_first_chunk_index < 0 || _first_chunk_index >= total_thunk_count)
    {
        env->ThrowError("SelectThunkEvery: first_chunk_index must be between 0 and total thunk count (%d).", total_thunk_count);
    }
    int stripped_thunk_count = total_thunk_count - _first_chunk_index;
    int new_thunk_count = roundup_div(stripped_thunk_count, _every);
    bool has_final_thunk = (new_thunk_count - 1) * _every + 1 == stripped_thunk_count;
    // we don't want to support audio
    vi.audio_samples_per_second = 0;
    vi.MulDivFPS(1, _every);
    if (has_final_thunk)
    {
        int last_thunk = vi.num_frames % _thunk_size;
        if (last_thunk == 0)
        {
            last_thunk = _thunk_size;
        }
        vi.num_frames = (new_thunk_count - 1) * _thunk_size + last_thunk;
    } else {
        vi.num_frames = new_thunk_count * _thunk_size;
    }
}

PVideoFrame __stdcall SelectThunkEvery::GetFrame(int n, IScriptEnvironment* env)
{
    if (n < 0 || n >= vi.num_frames)
    {
        env->ThrowError("SelectThunkEvery: Requested frame index out of range.");
    }
    int thunk_index = n / _thunk_size * _every;
    int frame_index = n % _thunk_size;
    return this->child->GetFrame((_first_chunk_index + thunk_index) * _thunk_size + frame_index, env);
}

SelectThunkEvery::~SelectThunkEvery()
{
}