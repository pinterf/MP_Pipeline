#include "common.h"

#define THUNKEDINTERLEAVE_SIMPLE_MACRO_NAME
#define THUNKEDINTERLEAVE_IMPLEMENTATION

#include "ThunkedInterleave.h"
#include <assert.h>

AVSValue __cdecl Create_ThunkedInterleave(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    AVSValue clips = ARG(clips);
    return new ThunkedInterleave(ThunkedInterleave_parameter_storage_t(args), clips, env);
}

ThunkedInterleave::ThunkedInterleave(const ThunkedInterleave_parameter_storage_t& o, AVSValue clips, IScriptEnvironment* env)
    : ThunkedInterleave_parameter_storage_t(o)
{
    assert(clips.ArraySize() > 0);

    memset(&_vi, 0, sizeof(_vi));
    memset(&_clips, 0, sizeof(_clips));

    if (_thunk_size <= 0)
    {
        env->ThrowError("SelectThunkEvery: thunk_size must be greater than 0.");
    }

    _clip_count = clips.ArraySize();
    if (_clip_count > MAX_CLIPS)
    {
        env->ThrowError("ThunkedInterleave: Too many clips.");
    }
    int framecount_full = 0;
    bool found_last_part = false;
    _vi = clips[0].AsClip()->GetVideoInfo();
    _vi.num_frames = 0;
    _vi.audio_samples_per_second = 0;
    for (int i = 0; i < _clip_count; i++)
    {
        _clips[i] = clips[i].AsClip();

        const VideoInfo& clip_vi = _clips[i]->GetVideoInfo();
        if (!clip_vi.IsSameColorspace(_vi) ||
             clip_vi.width != _vi.width ||
             clip_vi.height != _vi.height ||
             (clip_vi.image_type & 7) != (_vi.image_type & 7) // Seems someone uses this field to store flags?
            )
        {
            cleanup_clips();
            env->ThrowError("ThunkedInterleave: Clip #%d doesn't have correct parameters.", i);
        }
        if (framecount_full <= 0)
        {
            framecount_full = clip_vi.num_frames;
            if (framecount_full % _thunk_size != 0)
            {
                framecount_full = _thunk_size * (framecount_full / _thunk_size + 1);
                found_last_part = true;
            }
        } else if (clip_vi.num_frames > framecount_full) {
            cleanup_clips();
            env->ThrowError("ThunkedInterleave: Clip #%d has incorrect frame count (%d). It should be the same as or less than frame count of Clip #0 (%d).", i, clip_vi.num_frames, framecount_full);
        } else if (clip_vi.num_frames < framecount_full) {
            int diff = framecount_full - clip_vi.num_frames;
            assert(diff > 0);
            if (diff > _thunk_size)
            {
                cleanup_clips();
                env->ThrowError("ThunkedInterleave: Clip #%d has incorrect frame count (%d). It shouldn't be more than one thunk less than frame count of Clip #0 (%d).", i, clip_vi.num_frames, framecount_full);
            } else if (diff < _thunk_size) {
                if (found_last_part)
                {
                    cleanup_clips();
                    env->ThrowError("ThunkedInterleave: Clip #%d has incorrect frame count (%d). It should be one thunk less than frame count of Clip #0 since it is after the clip with last thunk (%d).", i, clip_vi.num_frames, framecount_full);
                }
                found_last_part = true;
            } else { // diff == _thunk_size
                found_last_part = true;
            }
        } else { // clip_vi.num_frames == framecount_full
            if (found_last_part)
            {
                cleanup_clips();
                env->ThrowError("ThunkedInterleave: Clip #%d has incorrect frame count (%d). It should be one thunk less than frame count of Clip #0 since it is after the clip with last thunk (%d).", i, clip_vi.num_frames, framecount_full);
            }
        }
        _vi.num_frames += clip_vi.num_frames;
    }
    _vi.MulDivFPS(_clip_count, 1);
}

void __stdcall ThunkedInterleave::GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env)
{
    // this shouldn't be called since we have disabled audio in the constructor
    assert(false);
}

PVideoFrame __stdcall ThunkedInterleave::GetFrame(int n, IScriptEnvironment* env)
{
    int clip_index = -1;
    int frame_number_in_sub_clip = -1;
    parse_frame_number(n, clip_index, frame_number_in_sub_clip);
    assert(clip_index >= 0);
    assert(frame_number_in_sub_clip >= 0);
    return _clips[clip_index]->GetFrame(frame_number_in_sub_clip, env);
}

bool __stdcall ThunkedInterleave::GetParity(int n)
{
    int clip_index = -1;
    int frame_number_in_sub_clip = -1;
    parse_frame_number(n, clip_index, frame_number_in_sub_clip);
    assert(clip_index >= 0);
    assert(frame_number_in_sub_clip >= 0);
    return _clips[clip_index]->GetParity(frame_number_in_sub_clip);
}

void ThunkedInterleave::parse_frame_number(int frame_number, int& clip_index, int& frame_number_in_sub_clip)
{
    int thunk_index = frame_number / _thunk_size;
    clip_index = thunk_index % _clip_count;
    frame_number_in_sub_clip = (thunk_index / _clip_count) * _thunk_size + (frame_number % _thunk_size);
}

void ThunkedInterleave::cleanup_clips()
{
    for (int i = 0; i < _clip_count; i++)
    {
        _clips[i] = NULL;
    }
}
ThunkedInterleave::~ThunkedInterleave()
{
    cleanup_clips();
}