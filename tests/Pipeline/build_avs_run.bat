@echo off
rem Builds the test host avs_run (avs_run.cpp) for Windows, 64 and 32 bit: bin\avs_run64.exe and
rem bin\avs_run32.exe. run_tests.ps1 runs the audio scenarios (audio_*.avs) with it and calls this
rem script when they are missing. Needs Visual Studio (C++ workload).
setlocal
set HERE=%~dp0
set SRC=%HERE%..\..\AVS_SplitProcess
set OUT=%HERE%bin

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (echo Visual Studio with C++ tools not found & exit /b 1)
set VCVARS="%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat"

if not exist "%OUT%\obj64" md "%OUT%\obj64"
if not exist "%OUT%\obj32" md "%OUT%\obj32"

set CLFLAGS=/nologo /EHsc /O2 /W3 /std:c++17 /I"%SRC%"

call %VCVARS% x64 >nul || exit /b 1
cl %CLFLAGS% "%HERE%avs_run.cpp" /Fe:"%OUT%\avs_run64.exe" /Fo:"%OUT%\obj64\\" >"%OUT%\avs_run64.log" 2>&1 || (type "%OUT%\avs_run64.log" & exit /b 1)
call %VCVARS% x86 >nul || exit /b 1
cl %CLFLAGS% "%HERE%avs_run.cpp" /Fe:"%OUT%\avs_run32.exe" /Fo:"%OUT%\obj32\\" >"%OUT%\avs_run32.log" 2>&1 || (type "%OUT%\avs_run32.log" & exit /b 1)
findstr /i /c:"warning" "%OUT%\avs_run64.log" "%OUT%\avs_run32.log"
echo Build OK
exit /b 0
