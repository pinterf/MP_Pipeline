#pragma once

#include "ThunkedInterleave.def.h"

#define MAX_CLIPS 255

class ThunkedInterleave : public ThunkedInterleave_parameter_storage_t, public IClip
{
public:
    ThunkedInterleave(const ThunkedInterleave_parameter_storage_t& o, AVSValue clips, IScriptEnvironment* env);
    ~ThunkedInterleave();

    void cleanup_clips();
    void parse_frame_number(int frame_number, int& clip_index, int& frame_number_in_sub_clip);

    virtual PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
    virtual bool __stdcall GetParity(int n);  // return field parity if field_based, else parity of first field in frame
    virtual void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env);
    virtual int __stdcall SetCacheHints(int cachehints, int frame_range) { return 0; };
    virtual const VideoInfo& __stdcall GetVideoInfo() { return _vi; };
private:
    VideoInfo _vi;
    PClip _clips[MAX_CLIPS + 1];
    int _clip_count;
};