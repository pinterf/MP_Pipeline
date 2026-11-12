# expect: MP_Pipeline: script block 1 must end with a clip
# a branched first block ends without a clip (later blocks always have the upstream clip in last)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
MP_Pipeline("""
x = ColorBars()
### branch: 2
### ###
""")
