rem Collects the release files into the distrib folder
rem Build first (each build also copies its dll and slave exe to the other platform's output
rem folder as the cross-bitness files .win32 / .win64):
rem   Release|Win32, Release|x64      Visual Studio 2026 (or 2022)
rem   ReleaseXP|Win32, ReleaseXP|x64  Visual Studio 2022, v141_xp toolset (VS 2026 has no v141_xp)
rem   build_arm64_mingw.bat           Windows on ARM64 (out\ARM64\llvm-mingw)
rem build_release.bat does all of this and calls this script.
if exist distrib rd /s /q distrib
md distrib
md distrib\x86
md distrib\x86_xp
md distrib\x64
md distrib\x64_xp
md distrib\arm64

xcopy /y Output\Win32\release\MP_Pipeline.dll distrib\x86\
xcopy /y Output\Win32\release\MP_Pipeline.dll.slave.exe distrib\x86\
xcopy /y Output\Win32\release\MP_Pipeline.dll.win64.slave.exe distrib\x86\
xcopy /y Output\Win32\release\MP_Pipeline.dll.win64 distrib\x86\

xcopy /y Output\Win32\releaseXP\MP_Pipeline.dll distrib\x86_xp\
xcopy /y Output\Win32\releaseXP\MP_Pipeline.dll.slave.exe distrib\x86_xp\
xcopy /y Output\Win32\releaseXP\MP_Pipeline.dll.win64.slave.exe distrib\x86_xp\
xcopy /y Output\Win32\releaseXP\MP_Pipeline.dll.win64 distrib\x86_xp\

xcopy /y Output\x64\release\MP_Pipeline.dll distrib\x64\
xcopy /y Output\x64\release\MP_Pipeline.dll.slave.exe distrib\x64\
xcopy /y Output\x64\release\MP_Pipeline.dll.win32.slave.exe distrib\x64\
xcopy /y Output\x64\release\MP_Pipeline.dll.win32 distrib\x64\

xcopy /y Output\x64\releaseXP\MP_Pipeline.dll distrib\x64_xp\
xcopy /y Output\x64\releaseXP\MP_Pipeline.dll.slave.exe distrib\x64_xp\
xcopy /y Output\x64\releaseXP\MP_Pipeline.dll.win32.slave.exe distrib\x64_xp\
xcopy /y Output\x64\releaseXP\MP_Pipeline.dll.win32 distrib\x64_xp\

xcopy /y out\ARM64\llvm-mingw\MP_Pipeline.dll distrib\arm64\
xcopy /y out\ARM64\llvm-mingw\MP_Pipeline.dll.slave.exe distrib\arm64\

xcopy /y Documentation\MP_Pipeline_readme_avs26.avs distrib\
xcopy /y Documentation\MP_Pipeline_readme_avsplus.avs distrib\
xcopy /y Documentation\MP_Pipeline_frameprops_demo.avs distrib\
xcopy /y LICENSE distrib\
xcopy /y README.md distrib\
