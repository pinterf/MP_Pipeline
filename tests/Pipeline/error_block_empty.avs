# expect: MP_Pipeline: script block 1 must end with a clip
# a block without any statement
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
MP_Pipeline("""
# nothing here
### ###
""")
