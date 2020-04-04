# MP_Pipeline

Plugin for Avisynth

Copy files from x86 (or x86_xp) folder to the plugins directory for 32 bits plugins
Copy files from x64 (or x64_xp) folder to the plugins directory for 64 bits plugins

MP_Pipeline 0.20

#######################################################
0.20 (20200404) pinterf
* Avisynth+ support
  Add version resource
  Build: move to VS2019, v142 and v141_xp toolset
  Cleanup folder structure

0.18.2: (20190114) realfinder
* Avisynth 2.6 Classic MT support (realfinder). ### setmtmode: <a>, <b> Translates into SetMTMode(a,b)

Change log:
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
Additonal work 2018 realfinder, 2020 by pinterf
Licensed under GPL (http://www.gnu.org/copyleft/gpl.html).

Links:
https://forum.doom9.org/showthread.php?t=163281
http://avisynth.nl/index.php/MP_Pipeline
