# MP_Pipeline CMake build: MinGW (GCC, Clang, llvm-mingw), also cross-compiled (Windows on ARM64),
# and Linux (GCC, Clang).
# MSVC uses the solution: AVS_SplitProcess.sln (Release: Visual Studio 2026, ReleaseXP: Visual Studio 2022).
#
# Examples:
#   cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release        (MSYS2 MinGW x64 / x86 shell, Linux)
#   build_arm64_mingw.bat                                          (Windows on ARM64, llvm-mingw)
#   -DMPP_BUILD_TESTS=ON: also the test host for tests/Pipeline/run_tests.sh (Linux)
#
# Output, Windows: MP_Pipeline.dll and MP_Pipeline.dll.slave.exe of one platform. A release folder
# also needs the other bitness pair (MP_Pipeline.dll.win32 / .win64 and the .slave.exe of that) for
# the "### platform" blocks; build_release.bat assembles the folders.
# Output, Linux: libmp_pipeline.so and libmp_pipeline.so.slave, installed together into
# <prefix>/lib/avisynth (the slave is found next to the plugin). No "### platform" there.
cmake_minimum_required(VERSION 3.16...3.31)

project("MP_Pipeline" LANGUAGES CXX)
include(GNUInstallDirs)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "MP_Pipeline builds for Windows and Linux (platform_win32 / platform_posix).")
endif()

option(MPP_BUILD_TESTS "Build the test host for tests/Pipeline/run_tests.sh (Linux)" OFF)

# Avoid uselessly linking to unused libraries
set(CMAKE_STANDARD_LIBRARIES "" CACHE STRING "" FORCE)
set(CMAKE_C_STANDARD_LIBRARIES "" CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES "" CACHE STRING "" FORCE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS FALSE)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# no SIMD code: the target processor is only reported
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" ARCHID)
message(STATUS "Target processor: ${ARCHID}")

if(MSVC)
  message(FATAL_ERROR "Use AVS_SplitProcess.sln for the MSVC builds (Release: Visual Studio 2026, ReleaseXP: Visual Studio 2022).")
endif()

if(WIN32)
  add_compile_definitions(WIN32 _CONSOLE _CRT_SECURE_NO_WARNINGS)
  # 32 bit: lib.def exports the undecorated AvisynthPluginInit3 for the __stdcall AvisynthPluginInit3@8
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--enable-stdcall-fixup")
else()
  # make sure there are no undefined symbols
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")
endif()

add_subdirectory("AVS_SplitProcess")

# uninstall target
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
