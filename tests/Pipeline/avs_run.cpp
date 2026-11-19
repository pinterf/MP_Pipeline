// avs_run: loads an AviSynth script and requests every frame once, in order (what AVSMeter -o
// does for the Windows tests). The test host of tests/Pipeline/run_tests.sh (Linux) and of the
// audio scenarios of run_tests.ps1 (Windows).
//
//   avs_run [-audio] [-chunk <samples>] [-plain] <script.avs> [avisynth library, default libavisynth.so / avisynth.dll]
//
//   -audio  after each frame, reads the audio samples of that frame (as AVSMeter -av does), then
//           reads the whole audio again in large requests (-chunk, default 500000 samples, larger
//           than the MP_Pipeline audio buffer), then steps back frame by frame from the last frame
//           (GetFrame and the audio of the frame, as a GUI stepping back), then seeks randomly (a
//           fixed sequence: frames with their audio, and audio ranges of random length, also
//           partly before the start or after the end). Prints the number of samples and a hash
//           of each pass, in one line:
//             Audio: <samples> samples, <channels> ch, type <sample_type>, frame reads <hash>,
//                    chunk reads <hash>, step back <hash>, random seek <hash>
//           or "Audio: none".
//   -plain  sets the global variable MPP_PLAIN = true before loading the script: the audio
//           scenarios then run their blocks without MP_Pipeline (the reference)
//
// Exit code 0: every frame was delivered; 1: an error, its message on stderr ("Error: ...", the
// AviSynth message includes "(<script>, line <n>)" as AVSMeter shows it); 2: usage / loading.

#include "avisynth.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
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

static int64_t audio_chunk = 500000;

// FNV-1a, 64 bit
static void hash_bytes(uint64_t& hash, const unsigned char* data, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
}
static const uint64_t HASH_START = 14695981039346656037ULL;

static void read_audio(PClip& clip, const VideoInfo& vi, int64_t start, int64_t end, std::vector<unsigned char>& buffer, uint64_t& hash, IScriptEnvironment* env)
{
    if (end <= start)
    {
        return;
    }
    const size_t bytes = (size_t)vi.BytesFromAudioSamples(end - start);
    if (buffer.size() < bytes)
    {
        buffer.resize(bytes);
    }
    clip->GetAudio(buffer.data(), start, end - start, env);
    hash_bytes(hash, buffer.data(), bytes);
}

// the audio samples of frame n (the last frame: up to the end of the audio)
static void read_frame_audio(PClip& clip, const VideoInfo& vi, int n, std::vector<unsigned char>& buffer, uint64_t& hash, IScriptEnvironment* env)
{
    const int64_t start = vi.AudioSamplesFromFrames(n);
    int64_t end = vi.AudioSamplesFromFrames(n + 1);
    if (n == vi.num_frames - 1 || end > vi.num_audio_samples)
    {
        end = vi.num_audio_samples;
    }
    read_audio(clip, vi, start, end, buffer, hash, env);
}

// xorshift64: the same "random" sequence on every run and platform
static uint64_t next_random(uint64_t& state)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

int main(int argc, char** argv)
{
    bool audio = false;
    bool plain = false;
    int arg = 1;
    for (; arg < argc && argv[arg][0] == '-'; arg++)
    {
        if (strcmp(argv[arg], "-audio") == 0) audio = true;
        else if (strcmp(argv[arg], "-plain") == 0) plain = true;
        else if (strcmp(argv[arg], "-chunk") == 0 && arg + 1 < argc && atoll(argv[arg + 1]) > 0) audio_chunk = atoll(argv[++arg]);
        else { fprintf(stderr, "unknown option: %s\n", argv[arg]); return 2; }
    }
    if (arg >= argc)
    {
        fprintf(stderr, "usage: avs_run [-audio] [-chunk <samples>] [-plain] <script.avs> [avisynth library]\n");
        return 2;
    }
    const char* script = argv[arg];
#ifdef _WIN32
    const char* library_name = arg + 1 < argc ? argv[arg + 1] : "avisynth.dll";
#else
    const char* library_name = arg + 1 < argc ? argv[arg + 1] : "libavisynth.so";
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
            if (plain)
            {
                env->SetGlobalVar("MPP_PLAIN", AVSValue(true));
            }
            printf("Loading script...\n");
            fflush(stdout);
            AVSValue script_arg(script);
            AVSValue value = env->Invoke("Import", AVSValue(&script_arg, 1));
            if (!value.IsClip())
            {
                fprintf(stderr, "Error: the script did not return a clip\n");
                result = 1;
            } else {
                PClip clip = value.AsClip();
                const VideoInfo vi = clip->GetVideoInfo();
                const int frames = vi.num_frames;
                const bool has_audio = audio && vi.HasAudio();
                std::vector<unsigned char> buffer;
                uint64_t frame_hash = HASH_START;
                for (int n = 0; n < frames; n++)
                {
                    PVideoFrame frame = clip->GetFrame(n, env);
                    if (has_audio)
                    {
                        read_frame_audio(clip, vi, n, buffer, frame_hash, env);
                    }
                }
                printf("Frames processed: %d\n", frames);
                if (audio)
                {
                    if (!has_audio)
                    {
                        printf("Audio: none\n");
                    } else {
                        uint64_t chunk_hash = HASH_START;
                        for (int64_t start = 0; start < vi.num_audio_samples; start += audio_chunk)
                        {
                            const int64_t end = start + audio_chunk < vi.num_audio_samples ? start + audio_chunk : vi.num_audio_samples;
                            read_audio(clip, vi, start, end, buffer, chunk_hash, env);
                        }
                        // stepping back frame by frame from the last frame, as a GUI does
                        uint64_t back_hash = HASH_START;
                        for (int n = frames - 1; n >= 0; n--)
                        {
                            PVideoFrame frame = clip->GetFrame(n, env);
                            read_frame_audio(clip, vi, n, buffer, back_hash, env);
                        }
                        // random seeks: frames with their audio, and audio ranges of 1 .. 300000
                        // samples from 2000 before the start to 2000 after the end
                        uint64_t seek_hash = HASH_START;
                        uint64_t state = 0x9E3779B97F4A7C15ULL;
                        for (int i = 0; i < 400; i++)
                        {
                            if (next_random(state) % 3 != 0)
                            {
                                const int n = (int)(next_random(state) % (uint64_t)frames);
                                PVideoFrame frame = clip->GetFrame(n, env);
                                read_frame_audio(clip, vi, n, buffer, seek_hash, env);
                            } else {
                                const int64_t count = 1 + (int64_t)(next_random(state) % 300000);
                                const int64_t start = -2000 + (int64_t)(next_random(state) % (uint64_t)(vi.num_audio_samples + 4000));
                                read_audio(clip, vi, start, start + count, buffer, seek_hash, env);
                            }
                        }
                        printf("Audio: %lld samples, %d ch, type %d, frame reads %016llx, chunk reads %016llx, step back %016llx, random seek %016llx\n",
                            (long long)vi.num_audio_samples, vi.AudioChannels(), vi.SampleType(),
                            (unsigned long long)frame_hash, (unsigned long long)chunk_hash,
                            (unsigned long long)back_hash, (unsigned long long)seek_hash);
                    }
                }
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
