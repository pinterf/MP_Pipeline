#include "common.h"
#include "MP_Pipeline.h"
#include "SelectThunkEvery.h"
#include "ThunkedInterleave.h"
#include "SharedMemoryClient.h"
#include "SharedMemoryServer.h"

const AVS_Linkage* AVS_linkage = nullptr;

#ifdef _WIN32
#define MPP_EXPORT __declspec(dllexport)
#else
// the other symbols are hidden (-fvisibility=hidden): they must not bind to another plugin's ones
#define MPP_EXPORT __attribute__((visibility("default")))
#endif

extern "C" MPP_EXPORT const char* __stdcall AvisynthPluginInit3(IScriptEnvironment * env, const AVS_Linkage* const vectors) {
  AVS_linkage = vectors;
  Register_MP_Pipeline(env);
  Register_SelectThunkEvery(env);
  Register_ThunkedInterleave(env);
  Register_SharedMemoryClient(env);
  Register_SharedMemoryServer(env);

  return "MP";
}
