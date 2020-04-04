#pragma once

#include "SharedMemoryClient.def.h"
#include "avisynth.h"
#include "SharedMemoryCommon.h"

#include <memory>

class SharedMemoryClient : public IClip, private SharedMemoryClient_parameter_storage_t
{
public:
    SharedMemoryClient(SharedMemoryClient_parameter_storage_t& o, IScriptEnvironment* env);
    ~SharedMemoryClient();
    
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
    bool __stdcall GetParity(int n);
    void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env);
    int __stdcall SetCacheHints(int cachehints,int frame_range);
    const VideoInfo& __stdcall GetVideoInfo();
private:
    PVideoFrame create_frame(int response_index, IScriptEnvironment* env);
    bool is_shutting_down();
    void check_shutting_down(IScriptEnvironment* env);

    bool _shutdown;
    SharedMemorySourceManager _manager;
    VideoInfo _vi;
};