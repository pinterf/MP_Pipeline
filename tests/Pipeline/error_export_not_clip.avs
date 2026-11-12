# expect: MP_Pipeline: script block 1: exported variable 'a' is not a clip
# an exported variable must be a clip
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
MP_Pipeline("""
ColorBars()
a = 5
### export clip: a
### ###
""")
