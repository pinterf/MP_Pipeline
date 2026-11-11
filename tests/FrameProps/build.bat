@echo off
rem Builds and runs the FrameProps serializer test, 64 and 32 bit.
rem Needs Visual Studio (C++ workload) and AviSynth+ (V11+) installed for both bitnesses.
rem Usage: build.bat [norun]
setlocal
set HERE=%~dp0
set SRC=%HERE%..\..\AVS_SplitProcess
set ASMLIB=%HERE%..\..\lib
set OUT=%HERE%bin

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (echo Visual Studio with C++ tools not found & exit /b 1)
set VCVARS="%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat"

if not exist "%OUT%\obj64" md "%OUT%\obj64"
if not exist "%OUT%\obj32" md "%OUT%\obj32"

set CLFLAGS=/nologo /EHsc /O2 /W4 /std:c++17 /I"%SRC%"
set FILES="%HERE%test_frameprops.cpp" "%SRC%\FrameProps.cpp"

call %VCVARS% x64 >nul || exit /b 1
cl %CLFLAGS% %FILES% /Fe:"%OUT%\test_frameprops64.exe" /Fo:"%OUT%\obj64\\" /link /LIBPATH:"%ASMLIB%" alibcof64.lib >"%OUT%\build64.log" 2>&1 || (type "%OUT%\build64.log" & exit /b 1)
call %VCVARS% x86 >nul || exit /b 1
cl %CLFLAGS% %FILES% /Fe:"%OUT%\test_frameprops32.exe" /Fo:"%OUT%\obj32\\" /link /LIBPATH:"%ASMLIB%" alibcof32.lib >"%OUT%\build32.log" 2>&1 || (type "%OUT%\build32.log" & exit /b 1)
findstr /i /c:"warning" "%OUT%\build64.log" "%OUT%\build32.log"
echo Build OK
if /i "%1"=="norun" exit /b 0

set RESULT=0
echo --- x64
"%OUT%\test_frameprops64.exe" || set RESULT=1
echo --- x86
"%OUT%\test_frameprops32.exe" || set RESULT=1
exit /b %RESULT%
