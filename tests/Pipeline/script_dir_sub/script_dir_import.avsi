# imported by script_dir.avs: MP_Pipeline called while this file is evaluated, the slaves must
# get this file's ScriptDir() etc.
Assert(RightStr(ScriptDir(), 15) == "script_dir_sub\", "script_dir_import.avsi: unexpected ScriptDir() " + ScriptDir())
global SCRIPT_DIR_IMPORTED = ScriptDirPipeline(ScriptDirChecks("script_dir_import.avsi"))
