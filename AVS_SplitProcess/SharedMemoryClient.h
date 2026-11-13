#pragma once

#include "SharedMemoryClient.def.h"
#include "avisynth.h"
#include "SharedMemoryCommon.h"

#include <atomic>
#include <memory>
#include <string>

class SharedMemoryClient : public IClip, private SharedMemoryClient_parameter_storage_t
{
public:
    SharedMemoryClient(const SharedMemoryClient_parameter_storage_t& o, IScriptEnvironment* env);
    ~SharedMemoryClient();
    
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
    bool __stdcall GetParity(int n);
    void __stdcall GetAudio(void* buf, int64_t start, int64_t count, IScriptEnvironment* env);
    int __stdcall SetCacheHints(int cachehints,int frame_range);
    const VideoInfo& __stdcall GetVideoInfo();
private:
    // GetFrame / GetParity without the handling of a lost server (PeerLostError)
    PVideoFrame get_frame(int n, IScriptEnvironment* env);
    bool get_parity(int n);
    // props_error receives a frame property transfer error; the caller reports it only after
    // finishing the slot bookkeeping, so the server is never left waiting for this client
    PVideoFrame create_frame(int response_index, IScriptEnvironment* env, std::string& props_error);
    void throw_if_props_error(const std::string& props_error, IScriptEnvironment* env);
    bool is_shutting_down();
    void check_shutting_down(IScriptEnvironment* env);

    bool _shutdown;
    std::atomic<bool> _server_lost; // the server's process is gone (killed, crashed)
    SharedMemorySourceManager _manager;
    VideoInfo _vi;
    FramePropsCaps _props_caps;
    std::unique_ptr<PropsReader> _props_readers[2];
};