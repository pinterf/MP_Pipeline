#pragma once

#include <string>

// workaround for mixed char & wchar_t environment


#ifdef UNICODE
#define tstring wstring
#define tostringstream wostringstream
#else
#define tstring string
#define tostringstream ostringstream
#endif

typedef wchar_t SYSCHAR;
#define sys_string std::wstring
#define SYSTEXT(x) L##x
