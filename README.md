# MP_Pipeline

Plugin for Avisynth

Copy files from x86 (or x86_xp) folder to the plugins directory for 32 bits plugins
Copy files from x64 (or x64_xp) folder to the plugins directory for 64 bits plugins

MP_Pipeline 2.0

Frame properties (AviSynth+)

Frame properties are passed between the processes (also between win32 and win64), when
both sides run AviSynth+ with frame property support (interface version 8 or later).
Integer, float and string/data properties (also arrays) are passed; clip and frame
typed properties are skipped. "### frame props: off" in a block switches it off for the
frames leaving that block.
See Documentation/MP_Pipeline_readme_avsplus.avs and MP_Pipeline_frameprops_demo.avs.

Tests: tests/Pipeline/run_tests.ps1 (AVSMeter based pipeline tests) and
tests/FrameProps (serializer unit test), see the comments in the files.

#######################################################
Change log:

2.0 (20261112) pinterf
* Frame properties are passed between the processes (AviSynth+, interface V8 or later on both sides),
  also between win32 and win64, with branch, prefetch, export clip and pass clip.
  Integer, float and data properties incl. arrays; the data type hint (binary/UTF-8) is kept when both
  sides have interface V11 or later. Clip and frame typed properties are skipped.
  Up to 4 KB of property data per frame travels with the frame, larger property sets use an extra
  shared memory buffer per frame slot (grows, shrinks and is released on demand), max. 256 MB per frame.
  MPP_TRACE_PROPS=1 environment variable: reports the extra buffer events with OutputDebugString.
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
* More than one "### platform" statement in a block is an error (the first one was used silently)
* AviSynth+ headers updated to interface V14
* Build: the filter definition generator runs with the Python launcher (py -3)
* Documentation: frame properties, pixel formats, ### frame props statement;
  new demo script Documentation/MP_Pipeline_frameprops_demo.avs
* Tests: tests/Pipeline (AVSMeter based pipeline tests, run_tests.ps1) and tests/FrameProps
  (frame property serializer unit test)
* Development of this release was assisted by Claude Code (Anthropic, Claude Opus 5.5): code, tests and
  documentation, based on pinterf's ideas and former test scripts, reviewed by pinterf

0.23 (20211008) pinterf
* Adapt to post-3.7.1test17 arrays: named arrays are undefined if they are not given like any other named parameter type.
  Code was explicitely checked for array-ness.
  (case of MPP_SharedMemoryServer  [aux_clips]c*, becasue Avisynth differs between an explicite zero sized array parameter
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

Web: 
http://www.nmm-hd.org/newbbs/viewtopic.php?f=7&t=498
http://forum.doom9.org/showthread.php?t=163281

Original Source code:
https://github.com/SAPikachu/MP_Pipeline

Mod:
https://github.com/pinterf/MP_Pipeline

Copyleft 2011-2012 SAPikachu (sapikachu@gmail.com, http://www.sapikachu.net)
Additonal work 2018 realfinder, 2020-21, 2026 by pinterf
Licensed under GPL (http://www.gnu.org/copyleft/gpl.html).

Links:
https://forum.doom9.org/showthread.php?t=163281
http://avisynth.nl/index.php/MP_Pipeline
