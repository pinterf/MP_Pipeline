#include "stdafx.h"
#include "MP_Pipeline.h"
#include "SelectThunkEvery.h"
#include "ThunkedInterleave.h"
#include "SharedMemoryClient.h"
#include "SharedMemoryServer.h"

const AVS_Linkage* AVS_linkage = nullptr;

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment * env, const AVS_Linkage* const vectors) {
  AVS_linkage = vectors;
  Register_MP_Pipeline(env);
  Register_SelectThunkEvery(env);
  Register_ThunkedInterleave(env);
  Register_SharedMemoryClient(env);
  Register_SharedMemoryServer(env);

  return "MP";
}
