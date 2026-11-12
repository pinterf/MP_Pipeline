# expect: MP_Pipeline: script block 1 must end with a clip
# the block ends with an assignment: "last" is not set
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
MP_Pipeline("""
x = ColorBars()
### ###
""")
