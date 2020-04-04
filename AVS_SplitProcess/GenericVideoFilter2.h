#include "avisynth.h"
#include <string.h>

class GenericVideoFilter2 : public IClip {
protected:
  PClip child;
  VideoInfo vi;
  void SetChild(PClip child) { this->child = child; this->vi = child->GetVideoInfo(); }
public:
  GenericVideoFilter2() { child = NULL; memset(&vi, 0, sizeof(vi)); }
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) { return child->GetFrame(n, env); }
  void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env) { child->GetAudio(buf, start, count, env); }
  const VideoInfo& __stdcall GetVideoInfo() { return vi; }
  bool __stdcall GetParity(int n) { return child->GetParity(n); }
  int __stdcall SetCacheHints(int cachehints, int frame_range) { return 0; };  // We do not pass cache requests upwards, only to the next filter.
};