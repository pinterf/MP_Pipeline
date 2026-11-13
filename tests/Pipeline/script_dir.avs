# ScriptDir(), ScriptFile(), ScriptName() (and the Utf8 versions) return the master's values in the slaves,
# native and other bitness (OTHER_PLATFORM); when MP_Pipeline is called from an imported file, that
# file's values (like in the master), see script_dir_sub/script_dir_import.avsi
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)

# script lines asserting that the script name functions return the current values of the caller
function ScriptDirChecks(string where)
{
  q = Chr(34)
  names = FunctionExists("ScriptDirUtf8") ? \
    ["ScriptDir", "ScriptFile", "ScriptName", "ScriptDirUtf8", "ScriptFileUtf8", "ScriptNameUtf8"] : \
    ["ScriptDir", "ScriptFile", "ScriptName"]
  values = FunctionExists("ScriptDirUtf8") ? \
    [ScriptDir(), ScriptFile(), ScriptName(), ScriptDirUtf8(), ScriptFileUtf8(), ScriptNameUtf8()] : \
    [ScriptDir(), ScriptFile(), ScriptName()]
  s = ""
  for (i = 0, ArraySize(names) - 1) {
    s = s + "Assert(Defined(" + names[i] + "()) && " + names[i] + "() == " + q + values[i] + q + ", " + \
      q + where + ": " + names[i] + "() is " + q + " + (Defined(" + names[i] + "()) ? " + names[i] + "() : " + q + "undefined" + q + "))" + Chr(10)
  }
  return s
}

# two slaves (native, other bitness) and the main block, each checking the values
function ScriptDirPipeline(string checks)
{
  return MP_Pipeline(checks + """BlankClip(length=800, width=64, height=64, pixel_type="YV12")
### ###
### platform: """ + OTHER_PLATFORM + Chr(10) + checks + """last
### ###
""" + checks + "last" + Chr(10))
}

function ScriptDirLine(int n) { return String(n) + " OK" }

main = ScriptDirPipeline(ScriptDirChecks("script_dir.avs"))
Import(ScriptDir() + "script_dir_sub/script_dir_import.avsi") # sets SCRIPT_DIR_IMPORTED
StackHorizontal(main, SCRIPT_DIR_IMPORTED)
WriteFile(LOG_DIR + "script_dir.log", "ScriptDirLine(current_frame)")
