# MP_Pipeline CMake build: MinGW (GCC, Clang, llvm-mingw), also cross-compiled (Windows on ARM64).
# MSVC uses the solution: AVS_SplitProcess.sln (Release: Visual Studio 2026, ReleaseXP: Visual Studio 2022).
# Windows only: Windows processes, shared memory and events.
#
# Examples:
#   cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release        (MSYS2 MinGW x64 / x86 shell)
#   build_arm64_mingw.bat                                          (Windows on ARM64, llvm-mingw)
#
# Output: MP_Pipeline.dll and MP_Pipeline.dll.slave.exe of one platform. A release folder also
# needs the other bitness pair (MP_Pipeline.dll.win32 / .win64 and the .slave.exe of that) for
# the "### platform" blocks; build_release.bat assembles the folders.
cmake_minimum_required(VERSION 3.16...3.31)

project("MP_Pipeline" LANGUAGES CXX)
include(GNUInstallDirs)

if(NOT WIN32)
  message(FATAL_ERROR "MP_Pipeline is Windows only (Windows processes, shared memory and events).")
endif()

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

add_compile_definitions(WIN32 _CONSOLE _CRT_SECURE_NO_WARNINGS)
# 32 bit: lib.def exports the undecorated AvisynthPluginInit3 for the __stdcall AvisynthPluginInit3@8
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--enable-stdcall-fixup")

add_subdirectory("AVS_SplitProcess")

# uninstall target
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
