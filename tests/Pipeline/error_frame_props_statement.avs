# expect: MP_Pipeline: Invalid frame props statement
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
MP_Pipeline("""
ColorBars()
### frame props: maybe
### ###
""")
