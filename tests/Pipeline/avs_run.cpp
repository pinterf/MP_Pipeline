// avs_run: loads an AviSynth script and requests every frame once, in order (what AVSMeter -o
// does for the Windows tests). For tests/Pipeline/run_tests.sh on Linux.
//
//   avs_run <script.avs> [avisynth library, default libavisynth.so]
//
// Exit code 0: every frame was delivered; 1: an error, its message on stderr ("Error: ...", the
// AviSynth message includes "(<script>, line <n>)" as AVSMeter shows it); 2: usage / loading.

#include "avisynth.h"

#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#define dlopen(name, flags) ((void*)LoadLibraryA(name))
#define dlsym(lib, name) ((void*)GetProcAddress((HMODULE)(lib), name))
#define dlerror() "LoadLibrary failed"
#else
#include <dlfcn.h>
#endif

const AVS_Linkage* AVS_linkage = nullptr;

typedef IScriptEnvironment* (__stdcall *CreateScriptEnvironment_t)(int);

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: avs_run <script.avs> [avisynth library]\n");
        return 2;
    }
#ifdef _WIN32
    const char* library_name = argc > 2 ? argv[2] : "avisynth.dll";
#else
    const char* library_name = argc > 2 ? argv[2] : "libavisynth.so";
#endif
    // RTLD_LOCAL: older cores export symbols (AVS_linkage, BitBlt...) that plugins define too; in the
    // global scope the plugins' own ones would bind to the core's (crash in their AvisynthPluginInit3)
    void* library = dlopen(library_name, RTLD_NOW | RTLD_LOCAL);
    if (!library)
    {
        fprintf(stderr, "Unable to load %s: %s\n", library_name, dlerror());
        return 2;
    }
    CreateScriptEnvironment_t create = (CreateScriptEnvironment_t)dlsym(library, "CreateScriptEnvironment");
    if (!create)
    {
        fprintf(stderr, "%s has no CreateScriptEnvironment\n", library_name);
        return 2;
    }
    // the newest interface the core and our headers know: an AviSynth+ older than the headers
    // refuses the higher numbers
    IScriptEnvironment* env = nullptr;
    for (int version = AVISYNTH_INTERFACE_VERSION; !env && version >= AVISYNTH_CLASSIC_INTERFACE_VERSION; version--)
    {
        try
        {
            env = create(version);
        } catch (const AvisynthError&) {
        }
    }
    if (!env)
    {
        fprintf(stderr, "CreateScriptEnvironment failed\n");
        return 2;
    }
    AVS_linkage = env->GetAVSLinkage();
    printf("%s\n", env->Invoke("VersionString", AVSValue(nullptr, 0)).AsString(""));

    int result = 0;
    {
        try
        {
            printf("Loading script...\n");
            fflush(stdout);
            AVSValue arg(argv[1]);
            AVSValue value = env->Invoke("Import", AVSValue(&arg, 1));
            if (!value.IsClip())
            {
                fprintf(stderr, "Error: the script did not return a clip\n");
                result = 1;
            } else {
                PClip clip = value.AsClip();
                const int frames = clip->GetVideoInfo().num_frames;
                for (int n = 0; n < frames; n++)
                {
                    PVideoFrame frame = clip->GetFrame(n, env);
                }
                printf("Frames processed: %d\n", frames);
            }
        } catch (const AvisynthError& e) {
            fprintf(stderr, "Error: %s\n", e.msg);
            result = 1;
        } catch (const IScriptEnvironment::NotFound&) {
            fprintf(stderr, "Error: function not found\n");
            result = 1;
        }
    }
    // the clips are released above, before the environment
    env->DeleteScriptEnvironment();
    return result;
}
