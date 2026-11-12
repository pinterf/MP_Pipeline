# a long ### inherit section (over 2000 characters, the 0.22 fix: the regex library crashed on some
# hundred characters), copied to a native, an other bitness (OTHER_PLATFORM) and the main block
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)

# 100 lines: global INH_<i> = <i*i>, a function using them, and a comment line per variable
global INH_COUNT = 100
inherit = "### inherit start ###" + Chr(10)
for (i = 1, INH_COUNT) {
  inherit = inherit + "# inherited value " + String(i) + Chr(10) + \
    "global INH_" + String(i) + " = " + String(i * i) + Chr(10)
}
inherit = inherit + "function InhSum() {" + Chr(10) + \
  "  s = 0" + Chr(10) + \
  "  for (i = 1, " + String(INH_COUNT) + ") { s = s + Eval(" + Chr(34) + "INH_" + Chr(34) + " + String(i)) }" + Chr(10) + \
  "  return s" + Chr(10) + \
  "}" + Chr(10) + \
  "### inherit end ###" + Chr(10)
Assert(StrLen(inherit) > 2000, "inherit section too short: " + String(StrLen(inherit)))

# sum of i*i for 1..100
check = """Assert(InhSum() == 338350, "InhSum() is " + String(InhSum()))
"""

MP_Pipeline(inherit + check + """BlankClip(length=800, width=64, height=64, pixel_type="YV12")
### ###
### platform: """ + OTHER_PLATFORM + Chr(10) + check + """Invert()
### ###
""" + check + "Invert()" + Chr(10))

function InhLine(int n) { return String(n) + " OK" }
WriteFile(LOG_DIR + "inherit_long.log", "InhLine(current_frame)")
