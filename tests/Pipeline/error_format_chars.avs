# expect: Script error: 100% sure: %s %d %n
# The slave error message is passed as data, never as a format string
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
MP_Pipeline("""
ColorBars()
Assert(false, "100% sure: %s %d %n")
### ###
""")
