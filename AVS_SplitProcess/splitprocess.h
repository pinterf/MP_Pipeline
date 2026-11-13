#pragma once

// Shared between the master (MP_Pipeline) and the slave program.

#include "SystemChar.h"

// the slave's script, passed in this environment variable (ANSI, as AviSynth reads script text)
#define SCRIPT_VAR_NAME_A "SPLITPROCESS_SCRIPT"

#define SLAVE_OK_FLAG "SLAVE_OK"

// The master's script name variables (the values ScriptDir(), ScriptName() etc. return there)
// are passed to the slaves in this environment variable, as UTF-8 "Name=value\n" lines.
// The environment block itself is UTF-16 on Windows, so it is converted only when the variable is
// set and read (the 'A' API would interpret the UTF-8 bytes in the ANSI code page).
// The slave sets both "$<Name>$" (ANSI) and "$<Name>Utf8$" from them.
#define SCRIPT_NAME_VARS_VAR_NAME SYSTEXT("MPP_SCRIPT_NAME_VARS")

static const char* const SCRIPT_NAME_VARS[] = {
    "ScriptName", "ScriptFile", "ScriptDir",
    "MainScriptName", "MainScriptFile", "MainScriptDir",
};