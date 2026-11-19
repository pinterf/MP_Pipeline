# expect: MP_Pipeline: Invalid audio statement
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
MP_Pipeline("""
ColorBars()
### audio: maybe
### ###
""")
