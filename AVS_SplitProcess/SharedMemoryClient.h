#pragma once

#include "SharedMemoryClient.def.h"
#include "avisynth.h"
#include "SharedMemoryCommon.h"

#include <memory>
#include <string>

class SharedMemoryClient : public IClip, private SharedMemoryClient_parameter_storage_t
{
public:
    SharedMemoryClient(const SharedMemoryClient_parameter_storage_t& o, IScriptEnvironment* env);
    ~SharedMemoryClient();
    
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
    bool __stdcall GetParity(int n);
    void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env);
    int __stdcall SetCacheHints(int cachehints,int frame_range);
    const VideoInfo& __stdcall GetVideoInfo();
private:
    // props_error receives a frame property transfer error; the caller reports it only after
    // finishing the slot bookkeeping, so the server is never left waiting for this client
    PVideoFrame create_frame(int response_index, IScriptEnvironment* env, std::string& props_error);
    void throw_if_props_error(const std::string& props_error, IScriptEnvironment* env);
    bool is_shutting_down();
    void check_shutting_down(IScriptEnvironment* env);

    bool _shutdown;
    SharedMemorySourceManager _manager;
    VideoInfo _vi;
    FramePropsCaps _props_caps;
    std::unique_ptr<PropsReader> _props_readers[2];
};