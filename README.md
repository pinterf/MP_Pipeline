# MP_Pipeline

MP_Pipeline splits an AviSynth script into blocks that run in separate processes. The frames travel
between the processes through shared memory; the last block of the script runs in the process that
calls MP_Pipeline (the host).

Typical uses:
- Parallel processing: "### branch" runs a block in several processes, "### prefetch" lets a block
  work ahead of the frame requests.
- Mixed bitness: a 32 bit filter in a 64 bit host, or the other way round ("### platform", Windows x86 / x64).
- A different AviSynth version for a block ("### dll").

Works with AviSynth+ and AviSynth 2.6 (classic). Platforms: Windows x86, x64 and ARM64, Linux x86-64.
Current version: 2.1, see the change log below.

## Contents

- [Installation](#installation)
- [Usage and statements](#usage-and-statements)
- [Frame properties, pixel formats and audio](#frame-properties-pixel-formats-and-audio)
- [Building](#building)
- [Tests](#tests)
- [Change log](#change-log)
- [Links and license](#links-and-license)

## Installation

Windows x86 / x64:
- Copy the files of the x86 (or x86_xp) folder to the plugin folder of the 32 bit AviSynth, the files of
  the x64 (or x64_xp) folder to the plugin folder of the 64 bit AviSynth.
- Keep all four files of a folder together: MP_Pipeline.dll and MP_Pipeline.dll.slave.exe, and the pair of
  the other bitness (MP_Pipeline.dll.win64 / .win32 and its .slave.exe), which the "### platform" blocks
  of the other bitness use.
- Mixed bitness needs both the 32 and the 64 bit AviSynth to be installed.
- The _xp builds run on Windows XP as well (Visual C++ v141_xp toolset).
- Runtime of the Visual Studio builds: Microsoft Visual C++ 2015-2022 Redistributable, 14.44 or newer
  (on Windows 7: 14.44.35211, the last version installing there, the same as AviSynth+ needs). The _xp
  builds need the last XP compatible one on XP, 14.27.29114 (14.28.29213 was reported to work too).

Windows on ARM64: MP_Pipeline.dll and MP_Pipeline.dll.slave.exe of the ARM64 build (llvm-mingw, built
by build_arm64_mingw.bat) next to each other in the plugin folder of the ARM64 AviSynth+. There is no
other bitness pair, "### platform:" is ignored.

Linux: libmp_pipeline.so and libmp_pipeline.so.slave next to each other in the AviSynth+ plugin folder
(cmake --install puts them into lib/avisynth), see Building.

## Usage and statements

```
LoadPlugin("MP_Pipeline.dll")  # not needed in an autoload folder
MP_Pipeline("""
ColorBars().ConvertToYV12().Trim(0, 999)
### prefetch: 5, 2
### ###
Blur(1.0)
""")
```

The line "### ###" ends a block. Every block but the last runs in its own slave process, the last one
runs in the host. A block starts with the last clip of the previous one (`last`); other clips are
handed over with "### export clip" and "### pass clip".

| Statement | What it does |
| --- | --- |
| `### ###` | Ends a block (the splitter line). |
| `### branch: n[, thunk size]` | Runs the block in n processes, each one produces a share of the frames. For spatial filters. |
| `### prefetch: max frames, frames behind` | The block works ahead of the requests, caching up to max frames. |
| `### export clip: a, b` | Makes clip variables (max. 8) of the block available in the next block. |
| `### pass clip: a` | Passes a clip received from upstream on: it is available in the next block as well. |
| `### platform: win32\|win64` | Runs the block in a slave of that bitness. Windows x86 / x64 only, ignored on ARM64 and Linux (a native slave). |
| `### dll: path` | The avisynth.dll (libavisynth.so) the block runs with. |
| `### inherit start ###` ... `### inherit end ###` | Copies the lines between them into all downstream blocks, e.g. LoadPlugin calls. |
| `### frame props: on\|off` | Passes the frame properties downstream or not (default: on). |
| `### audio: on\|off` | Passes the audio downstream or not (default: on). |
| `### setmtmode: a, b` | SetMTMode(a, b) in the block (AviSynth 2.6 classic MT). |
| `### lock threads to cores` | Accepted and ignored since 2.0. |

Every statement is described, with a full example script, in Documentation/MP_Pipeline_readme_avsplus.avs
(and MP_Pipeline_readme_avs26.avs for AviSynth 2.6).

## Frame properties, pixel formats and audio

With AviSynth+ the frame properties travel with the frames (also between win32 and win64), and all pixel
formats can be passed, including YA, 4:4:0, and 4:1:1 / 4:1:0 of interface V13. The audio is passed
too, in any sample type and channel count. The requirements and the limits are in the documentation
script mentioned above; Documentation/MP_Pipeline_frameprops_demo.avs and
MP_Pipeline_audio_demo.avs are examples.

## Building

- Visual Studio: AVS_SplitProcess.sln. Release (x86, x64): Visual Studio 2026 (v143 toolset);
  ReleaseXP: Visual Studio 2022 (v141_xp toolset, VS 2026 has none).
  The Visual Studio build copies the files of the other bitness pair to the other platform's Output folder.
- CMake (MinGW GCC / Clang, llvm-mingw): CMakeLists.txt, e.g. in an MSYS2 MinGW shell
    cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
  The C++ runtime is linked statically. MSVC uses the solution.
  A MinGW build works with a MinGW built AviSynth+ only (C++ exceptions of the AviSynth interface).
- Windows on ARM64: build_arm64_mingw.bat, cross-compiled on x64 Windows with llvm-mingw
  (https://github.com/mstorsjo/llvm-mingw, UCRT flavour; set LLVM_MINGW_ROOT), CMake and Ninja.
  Output: out\ARM64\llvm-mingw. The toolchain file is cmake/toolchains/llvm-mingw-aarch64.cmake.
- Linux (GCC, Clang): the same CMakeLists.txt
    cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build && sudo cmake --install build
  Output: libmp_pipeline.so and libmp_pipeline.so.slave, installed together into lib/avisynth (the
  slave must be next to the plugin). "### dll:" takes a libavisynth.so path. Tested on Ubuntu 26.04
  (GCC 15, Clang 21, AviSynth+ 3.7.6) and Ubuntu 22.04 (GCC 13, AviSynth+ 3.7.5).

## Tests

- tests/Pipeline/run_tests.ps1: pipeline tests on Windows (AVSMeter based; the audio scenarios run with
  the avs_run test host, built by build_avs_run.bat).
- tests/Pipeline/run_tests.sh: the same on Linux, with the avs_run test host (cmake -DMPP_BUILD_TESTS=ON).
- tests/FrameProps: serializer unit test.
See the comments in the files.

## Change log

In short (2.0 and 2.1):
  - Audio is passed between the processes (2.1)
  - Frame properties are passed between the processes
  - AviSynth+ interface V13 pixel formats: YA, 4:4:0, 4:1:1 / 4:1:0
  - Portable source, no longer tied to MSVC: builds with MSVC, ClangCL, GCC and Clang (MinGW, llvm-mingw)
  - CMake build (CMakeLists.txt) next to the Visual Studio solution: MinGW, llvm-mingw and Linux
  - Windows on ARM64 (aarch64) build
  - Linux support (x86-64)
  - Many stability fixes and clearer error messages

2.1 (20261119) pinterf
* Audio is passed between the processes (up to 2.0 it was removed): every sample type and channel
  count, also between win32 and win64, with branch (the audio of the first branch process), prefetch,
  export clip and pass clip. The GetAudio requests of the host are passed upstream unchanged, in any
  order (playing, stepping back, seeking); a request larger than the 1 MB audio buffer is split into parts.
  A clip without video (audio only) is passed as well.
* New statement: ### audio: on|off (default: on). "off" passes the clips of the block downstream
  without audio. (new MPP_SharedMemoryServer parameter: audio)
* The shared memory layout changed: MP_Pipeline.dll, its .slave.exe and the other bitness pair must
  be from the same version (a mismatch is reported).
* Tests: audio scenarios (tests/Pipeline/audio_*.avs) with the avs_run test host, on Windows too
  (build_avs_run.bat). The audio is read frame by frame, in large requests, stepping back and with
  random seeks, and compared with the same script run without MP_Pipeline.
* Documentation: audio; new demo script Documentation/MP_Pipeline_audio_demo.avs.
  MP_Pipeline_readme_avs26.avs: "### lock threads to cores" is accepted and ignored since 2.0.
* Build: NOMINMAX before the Windows headers (no min / max macros).
* Build: build_release.bat builds the x86 / x64 Release with the v143 toolset again (the 2.0 package
  was built with v145): it runs with the VC++ 14.44 runtime, the last one for Windows 7.

2.0 (20261118) pinterf
* Frame properties are passed between the processes (AviSynth+, interface V8 or later on both sides),
  also between win32 and win64, with branch, prefetch, export clip and pass clip.
  Integer, float and data properties incl. arrays; the data type hint (binary/UTF-8) is kept when both
  sides have interface V11 or later. Clip and frame typed properties are skipped.
  Up to 4 KB of property data per frame travels with the frame, larger property sets use an extra
  shared memory buffer per frame slot (grows, shrinks and is released on demand), max. 256 MB per frame.
  MPP_TRACE_PROPS=1 environment variable: reports the extra buffer events with OutputDebugString
  (on Linux to stderr).
* New statement: ### frame props: on|off
  "off" passes the frames of the block without frame properties (default: on).
  (new MPP_SharedMemoryServer parameter: props)
* Support the pixel formats of AviSynth+ interface V13: YA (Y plus alpha), 4:4:0, and 4:1:1 / 4:1:0
  with more than 8 bits or with alpha. YA clips could not be passed before.
  Such a clip needs AviSynth+ with interface V13 or later in the receiving process too, otherwise a
  clear error message is shown.
* ScriptDir(), ScriptFile(), ScriptName() and their Utf8 versions work in the slave processes too,
  returning the values of the script calling MP_Pipeline (they were undefined in the slaves).
  Also the SCRIPTDIR / MAINSCRIPTDIR prefixes of AddAutoloadDir.
* Fix: empty "Unable to create slave process. Message:" when the slave failed after printing other
  output (e.g. LSMash indexing progress), or when it exited without a message. The slave's last error
  message is shown now, or its exit code.
* Clear error messages instead of "Invalid arguments to function 'MPP_PrepareDownstreamClip'":
  "script block N must end with a clip" and "script block N: exported variable 'x' is not a clip"
* Exporting more than 8 clips from a block: the error message tells the block number and the count
  ("script block N exports X clips, max. 8 are allowed"), it is checked before any slave starts
* Fix: a slave killed or crashed while running no longer hangs the master and the other slaves: the
  waiting side notices the lost process within about 0.3 s, and the error "The upstream slave process
  ended unexpectedly." reaches the host; every slave ends and the shared memory is removed
* More than one "### platform" statement in a block is an error (the first one was used silently)
* AviSynth+ headers updated to interface V14
* Build: Visual Studio 2026 (v143 toolset) for x86 / x64; the XP builds (ReleaseXP, v141_xp toolset) need
  Visual Studio 2022, VS 2026 has no v141_xp toolset. Fix: building the solution in ReleaseXP
  configuration built the Release (non-XP) projects.
* Build: the filter definition generator runs with the Python launcher (py -3)
* Source: standard C++ instead of MSVC-only constructs (__try/__finally, temporaries bound to non-const
  references, __declspec(align)); builds with ClangCL as well, compiles with MinGW GCC and llvm-mingw
  (x86, x64, aarch64). The shared memory layout is the same with every compiler (compile-time checks).
* Build: no external libraries. The asmlib memory/string functions (lib/alibcof*.lib) are replaced by
  the standard ones; the unused AviSynth import
  libraries and the lib folder are removed. Links only system libraries (rpcrt4, psapi).
* Source: synchronization with standard C++ (std::atomic, std::mutex) instead of the Interlocked
  functions, volatile and critical sections. The spin locks in the shared memory take and release
  with acquire / release ordering: correct with every compiler and CPU (the old volatile store
  relied on MSVC's x86/x64 behavior, a prerequisite for ARM64). The shared memory layout is unchanged.
* "### lock threads to cores" is accepted and ignored: no more thread pinning, the operating system
  schedules the threads (better suited to today's hybrid and many-core CPUs).
  The fetcher_thread_lock_to_cpu / server_thread_lock_to_cpu parameters of MPP_SharedMemoryServer
  are ignored as well.
* Linux support (x86-64, GCC and Clang, AviSynth+ 3.7.5 and 3.7.6): the platform layer has a POSIX
  implementation (shared memory with shm_open, process-shared robust pthread events, posix_spawn, the
  slave ends with the master); libmp_pipeline.so with libmp_pipeline.so.slave next to it.
  "### platform:" is Windows x86 / x64 only, ignored on Linux (a native slave: scripts run unchanged).
  Tests: tests/Pipeline/run_tests.sh with the avs_run test host.
* Build: CMake build for MinGW (GCC, Clang) and llvm-mingw. A MinGW build needs a MinGW built
  AviSynth+ in its process (the C++ exceptions of the AviSynth interface differ between MSVC and
  MinGW), the MSVC builds an MSVC built one.
* Windows on ARM64 build (build_arm64_mingw.bat, cross-compiled with llvm-mingw) for the llvm-mingw
  built ARM64 AviSynth+. "### platform:" is ignored there (a native slave). Tested on Windows 11 ARM64
  with AviSynth+ 3.7.6.
* Documentation: frame properties, pixel formats, ### frame props statement;
  new demo script Documentation/MP_Pipeline_frameprops_demo.avs
* Tests: tests/Pipeline (AVSMeter based pipeline tests, run_tests.ps1) and tests/FrameProps
  (frame property serializer unit test)
* Development of this release was assisted / co-authored by Claude Code (Anthropic) and occasionally
  other AIs: code, tests and documentation, based on pinterf's ideas, organization and former test
  scripts, directed and reviewed by pinterf.

0.23 (20211008) pinterf
* Adapt to post-3.7.1test17 arrays: named arrays are undefined if they are not given like any other named parameter type.
  Code was explicitely checked for array-ness.
  (case of MPP_SharedMemoryServer  [aux_clips]c*, because Avisynth differs between an explicite zero sized array parameter
   and a not-given parameter. Internally generated MPP script does not pass the parameter by default)

0.22 (20201115) pinterf
* Fix crash when there were too many characters (over some hundred) in script between
  inherit start and inherit end markers (regex library limitation)

0.21 (20200605) pinterf
* Fix buffer overflow for alpha-aware YUVA420 and YUVA422
* Fix passing packed RGB formats

0.20 (20200404) pinterf
* Avisynth+ support
  Add version resource
  Build: move to VS2019, v142 and v141_xp toolset
  Cleanup folder structure

0.18.2: (20190114) realfinder
* Avisynth 2.6 Classic MT support (realfinder). ### setmtmode: a, b Translates into SetMTMode(a,b)

0.18
* Fix deadlock when exported clip is consumed by multiple script block

0.17
* Properly terminate slave processes when initialization fails
* Fix "Not a clip" error when using ### inherit and the last block is empty

0.16
* Try to silent all error dialogs on exit of slave process
* Slave process shouldn't be stuck on exit anymore, it will terminate itself if it doesn't exit cleanly after 15 seconds
* Fix ### branch statement, previously it incorrectly rejects some input

0.15
* Properly clean script environment up on exit
* Allow using different avisynth dll to run script block (### dll)

0.14
* Fixed another crashing bug

0.13
* Fixed a bug that causes occasional crashing

0.12
* Fixed a problem that makes scripts unable to be loaded in some programs

0.11
* Greatly improved performance, maximum 80% overhead reduction
* New feature: Ability to lock threads to cores, may improve performance in some cases
* (0.10 is skipped to avoid confusion)

0.9
* New feature: Frame prefetching
* New feature: Exporting multiple clip variables in a single process
* New feature: Code block can be shared between processes

0.3
* Binaries in the x86 folder are in correct version now (In 0.2 the win64 slave is actually win32...)
* Integrated a patched TCPDeliver, no longer depend on the external one
* Fixed random crash when filter chain is destroyed
* Thunked branching

0.2
* x64 support (please copy TCPDeliver.dll in the package to respective plugin folder)
* x86/x64 mixed slave process (requires both x86/x64 version of AviSynth to be installed)
* Add a script variable in branch slave process, make it distinguishable in script

## Links and license

- Forum: https://forum.doom9.org/showthread.php?t=163281 and
  http://www.nmm-hd.org/newbbs/viewtopic.php?f=7&t=498
- AviSynth wiki: http://avisynth.nl/index.php/MP_Pipeline
- Original source code: https://github.com/SAPikachu/MP_Pipeline
- This version: https://github.com/pinterf/MP_Pipeline

Copyleft 2011-2012 SAPikachu (sapikachu@gmail.com, http://www.sapikachu.net)
Additional work 2018 realfinder, 2020-21, 2026 by pinterf
Licensed under GPL (http://www.gnu.org/copyleft/gpl.html).
