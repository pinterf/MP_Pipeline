#pragma once

// The character type of the operating system's names (kernel objects, files, environment):
// UTF-16 (wchar_t) on Windows, UTF-8 (char) elsewhere. Conversions: platform.h.

#include <sstream>
#include <string>

#ifdef _WIN32
typedef wchar_t SYSCHAR;
typedef std::wstring sys_string;
typedef std::wostringstream sys_ostringstream;
#define SYSTEXT(x) L##x
#else
typedef char SYSCHAR;
typedef std::string sys_string;
typedef std::ostringstream sys_ostringstream;
#define SYSTEXT(x) x
#endif

// an ASCII text (names, numbers) as a sys_string
inline sys_string ascii_to_sys(const std::string& s)
{
    return sys_string(s.begin(), s.end());
}