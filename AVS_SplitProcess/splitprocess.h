
#pragma once

#include <Windows.h>
#include <string>

#define SCRIPT_VAR_NAME_A "SPLITPROCESS_SCRIPT"
#define SCRIPT_VAR_NAME_U L ## SCRIPT_VAR_NAME_A

#ifdef UNICODE
#define SCRIPT_VAR_NAME SCRIPT_VAR_NAME_U
#else
#define SCRIPT_VAR_NAME SCRIPT_VAR_NAME_A
#endif

#define SLAVE_OK_FLAG "SLAVE_OK"

// The master's script name variables (the values ScriptDir(), ScriptName() etc. return there)
// are passed to the slaves in this environment variable, as UTF-8 "Name=value\n" lines.
// The environment block itself is UTF-16, so it is converted only when the variable is set and
// read (the 'A' API would interpret the UTF-8 bytes in the ANSI code page).
// The slave sets both "$<Name>$" (ANSI) and "$<Name>Utf8$" from them.
#define SCRIPT_NAME_VARS_VAR_NAME_W L"MPP_SCRIPT_NAME_VARS"

static const char* const SCRIPT_NAME_VARS[] = {
    "ScriptName", "ScriptFile", "ScriptDir",
    "MainScriptName", "MainScriptFile", "MainScriptDir",
};

// Conversions, like the ones in AviSynth+ strings.cpp; ANSI is the code page AviSynth+ uses
// for the ANSI script names (AreFileApisANSI() ? CP_ACP : CP_OEMCP)
inline UINT ansi_code_page() { return AreFileApisANSI() ? CP_ACP : CP_OEMCP; }

inline std::wstring MultiByteToWide(const std::string& s, UINT code_page)
{
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(code_page, 0, s.data(), (int)s.size(), NULL, 0);
    std::wstring result(len, L'\0');
    MultiByteToWideChar(code_page, 0, s.data(), (int)s.size(), &result[0], len);
    return result;
}

inline std::string WideToMultiByte(const std::wstring& s, UINT code_page)
{
    if (s.empty()) return std::string();
    int len = WideCharToMultiByte(code_page, 0, s.data(), (int)s.size(), NULL, 0, NULL, NULL);
    std::string result(len, '\0');
    WideCharToMultiByte(code_page, 0, s.data(), (int)s.size(), &result[0], len, NULL, NULL); // replaces out-of-CP chars by ?
    return result;
}

inline std::wstring Utf8ToWideChar(const std::string& s) { return MultiByteToWide(s, CP_UTF8); }
inline std::string WideCharToUtf8(const std::wstring& s) { return WideToMultiByte(s, CP_UTF8); }
inline std::string AnsiToUtf8(const std::string& s) { return WideCharToUtf8(MultiByteToWide(s, ansi_code_page())); }
inline std::string Utf8ToAnsi(const std::string& s) { return WideToMultiByte(Utf8ToWideChar(s), ansi_code_page()); }