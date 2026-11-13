#pragma once

// Common header, included first by every source file.

// Target Windows version: the Windows headers hide newer APIs, so using one is a compile error.
// x86 / x64: Windows XP, the XP builds (v141_xp) run there and the other builds share the code.
// ARM64: Windows 10, the first Windows on ARM64.
#if defined(_M_ARM64) || defined(__aarch64__)
#define _WIN32_WINNT 0x0A00
#else
#define _WIN32_WINNT 0x0501
#endif
#define WINVER _WIN32_WINNT

#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <tchar.h>

#include <Windows.h>