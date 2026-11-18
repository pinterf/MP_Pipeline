@echo off
setlocal EnableDelayedExpansion

rem ============================================================
rem  MP_Pipeline release build
rem
rem  Builds all release variants, collects them (make_distrib.bat) into
rem  the distrib folder and packs it into a 7-Zip archive:
rem    x86, x64        Release, Visual Studio 2026 (toolset v145)
rem    x86_xp, x64_xp  ReleaseXP, Visual Studio 2022 (v141_xp: VS 2026 has no v141_xp toolset)
rem    arm64           Windows on ARM64, cross-compiled with llvm-mingw
rem                    (build_arm64_mingw.bat, needs Ninja)
rem
rem  Switches, 1 = build (default), 0 = skip; set them before the run,
rem  e.g. "set BUILD_XP=0 && build_release.bat":
rem    BUILD_VS  BUILD_XP  BUILD_ARM64
rem  A missing VS 2022 or llvm-mingw skips that variant with a warning,
rem  unless its switch was set explicitly. make_distrib.bat collects what
rem  it finds, so a skipped variant is missing from the package.
rem
rem  Detected, can be overridden from the environment:
rem    VER         FILEVERSION of AVS_SplitProcess\AVS_SplitProcess.rc (2,0,0,0 -> 2.0)
rem    BUILD_DATE  today, YYYYMMDD
rem  Output: distrib\ and build\release\MP_Pipeline_v<VER>.7z
rem
rem  The Visual Studio builds are Rebuilds of the solution into Output\<platform>\<configuration>.
rem  Each build also copies its dll and slave exe to the other platform's folder (the cross-bitness
rem  files), so the Win32 and x64 builds of a configuration belong together.
rem ============================================================

set "SRC=%~dp0"
set "SRC=%SRC:~0,-1%"
if not defined LLVM_MINGW_ROOT set "LLVM_MINGW_ROOT=C:\avsplus_build_deps\llvm-mingw-20261020-ucrt-x86_64"
set "SEVENZIP=%ProgramFiles%\7-Zip\7z.exe"
set "SLN=%SRC%\AVS_SplitProcess.sln"

set "_OVR_XP=%BUILD_XP%"
set "_OVR_ARM64=%BUILD_ARM64%"
if not defined BUILD_VS    set "BUILD_VS=1"
if not defined BUILD_XP    set "BUILD_XP=1"
if not defined BUILD_ARM64 set "BUILD_ARM64=1"

rem ============================================================
rem  DETECTION
rem ============================================================

rem  Version: "FILEVERSION 2,0,0,0", trailing ".0" parts are dropped (2.0.0.0 -> 2.0, 2.1.1.0 -> 2.1.1), at least major.minor stays
if not defined VER (
  for /f "tokens=2" %%A in ('findstr /r /c:"^ FILEVERSION  *[0-9]" "%SRC%\AVS_SplitProcess\AVS_SplitProcess.rc"') do set "VER=%%A"
  if defined VER (
    set "VER=!VER:,=.!"
    for /l %%I in (1,1,2) do if "!VER:~-2!"==".0" set "VER=!VER:~0,-2!"
  )
)
if not defined VER (
  echo ERROR: no FILEVERSION found in AVS_SplitProcess\AVS_SplitProcess.rc
  goto :fatal_exit
)
if not defined BUILD_DATE (
  for /f %%D in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd"') do set "BUILD_DATE=%%D"
)

rem  MSBuild of VS 2026 (18.x) and VS 2022 (17.x); for VS 2022 the first installation with the
rem  v141_xp toolset files. vswhere writes into a temp file: its version range argument has a
rem  comma and a parenthesis, which a for /f command line does not take unescaped.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere.exe not found: !VSWHERE!
  goto :fatal_exit
)
set "VSW_TMP=%TEMP%\mp_pipeline_build_release_vswhere.txt"
set "MSBUILD_2026="
set "MSBUILD_2022="
"%VSWHERE%" -version [18.0,19.0) -products * -prerelease -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe > "%VSW_TMP%" 2>nul
set /p MSBUILD_2026=<"%VSW_TMP%"
"%VSWHERE%" -version [17.0,18.0) -products * -requires Microsoft.VisualStudio.Component.WinXP -property installationPath > "%VSW_TMP%" 2>nul
for /f "usebackq delims=" %%I in ("%VSW_TMP%") do (
  if not defined MSBUILD_2022 if exist "%%I\MSBuild\Microsoft\VC\v150\Platforms\Win32\PlatformToolsets\v141_xp" if exist "%%I\MSBuild\Microsoft\VC\v170\Microsoft.Cpp.Default.props" set "MSBUILD_2022=%%I\MSBuild\Current\Bin\MSBuild.exe"
)
del "%VSW_TMP%" 2>nul

if "%BUILD_VS%"=="1" if not defined MSBUILD_2026 (
  echo ERROR: Visual Studio 2026 MSBuild not found ^(BUILD_VS=0 skips the Release builds^)
  goto :fatal_exit
)
if "%BUILD_XP%"=="1" if not defined MSBUILD_2022 (
  if defined _OVR_XP (
    echo ERROR: BUILD_XP was set, but no Visual Studio 2022 with v141_xp was found
    goto :fatal_exit
  )
  echo WARNING: no Visual Studio 2022 with v141_xp found, skipping the ReleaseXP builds
  set "BUILD_XP=0"
)
if "%BUILD_ARM64%"=="1" if not exist "%LLVM_MINGW_ROOT%\bin\aarch64-w64-mingw32-clang++.exe" (
  if defined _OVR_ARM64 (
    echo ERROR: BUILD_ARM64 was set, but llvm-mingw was not found at: !LLVM_MINGW_ROOT!
    goto :fatal_exit
  )
  echo WARNING: llvm-mingw not found at !LLVM_MINGW_ROOT!, skipping the ARM64 build
  set "BUILD_ARM64=0"
)

set "ARCHIVE_DIR=%SRC%\build\release"
set "ARCHIVE=%ARCHIVE_DIR%\MP_Pipeline_v%VER%.7z"

set TOTAL_BUILDS=0
if "%BUILD_VS%"=="1"    set /a TOTAL_BUILDS+=2
if "%BUILD_XP%"=="1"    set /a TOTAL_BUILDS+=2
if "%BUILD_ARM64%"=="1" set /a TOTAL_BUILDS+=1
set BUILDNUM=0

echo.
echo ========================================================
echo  MP_Pipeline release : %VER%  (%BUILD_DATE%)
echo  Archive             : %ARCHIVE%
echo  Release=%BUILD_VS%  ReleaseXP=%BUILD_XP%  ARM64=%BUILD_ARM64%
echo  VS 2026 MSBuild     : !MSBUILD_2026!
echo  VS 2022 MSBuild     : !MSBUILD_2022!
echo ========================================================
echo.

rem ============================================================
rem  BUILDS
rem ============================================================

if not "%BUILD_VS%"=="1" goto :skip_vs
call :vs_build "%MSBUILD_2026%" Release Win32 "-p:PlatformToolset=v145" || goto :fatal_exit
call :vs_build "%MSBUILD_2026%" Release x64 "-p:PlatformToolset=v145" || goto :fatal_exit
:skip_vs

if not "%BUILD_XP%"=="1" goto :skip_xp
call :vs_build "%MSBUILD_2022%" ReleaseXP Win32 || goto :fatal_exit
call :vs_build "%MSBUILD_2022%" ReleaseXP x64 || goto :fatal_exit
:skip_xp

if not "%BUILD_ARM64%"=="1" goto :skip_arm64
set /a BUILDNUM+=1
echo.
echo [BUILD %BUILDNUM%/%TOTAL_BUILDS%] ARM64 llvm-mingw
call "%SRC%\build_arm64_mingw.bat" || goto :fatal_exit
:skip_arm64

rem ============================================================
rem  COLLECT AND ARCHIVE
rem ============================================================
echo.
echo [DISTRIB] make_distrib.bat
pushd "%SRC%"
call "%SRC%\make_distrib.bat" > nul || ( popd & echo ERROR: make_distrib.bat failed & goto :fatal_exit )
popd
dir /s /b "%SRC%\distrib\*.dll" "%SRC%\distrib\*.exe"

if not exist "%SEVENZIP%" (
  echo WARNING: 7-Zip not found at !SEVENZIP!, no archive created
  goto :done
)
echo.
echo [ARCHIVE] %ARCHIVE%
if not exist "%ARCHIVE_DIR%" mkdir "%ARCHIVE_DIR%"
if exist "%ARCHIVE%" del "%ARCHIVE%"
"%SEVENZIP%" a -mx=9 "%ARCHIVE%" "%SRC%\distrib\*" > nul
if errorlevel 1 (
  echo ERROR: 7-Zip failed
  goto :fatal_exit
)

:done
echo.
echo ========================================================
echo  RELEASE BUILD COMPLETE: %SRC%\distrib
echo ========================================================
endlocal
exit /b 0

rem  "exit /b 1" inside a parenthesised block does not reliably set the exit
rem  code, so the error paths jump here.
:fatal_exit
echo.
echo RELEASE BUILD FAILED
endlocal
exit /b 1

rem ------------------------------------------------------------
rem  :vs_build <msbuild> <configuration> <platform> [extra msbuild properties]
rem ------------------------------------------------------------
:vs_build
set /a BUILDNUM+=1
echo.
echo [BUILD %BUILDNUM%/%TOTAL_BUILDS%] %~2 %~3
"%~1" "%SLN%" -t:Rebuild -p:Configuration=%~2 -p:Platform=%~3 %~4 -m -nologo -v:minimal -nodeReuse:false
if errorlevel 1 (
  echo ERROR: build failed: %~2 %~3
  exit /b 1
)
for %%F in ("%SRC%\Output\%~3\%~2\MP_Pipeline.dll" "%SRC%\Output\%~3\%~2\MP_Pipeline.dll.slave.exe") do (
  if not exist "%%~F" (
    echo ERROR: missing: %%~F
    exit /b 1
  )
  echo   %%~tF  %%~nxF
)
exit /b 0
