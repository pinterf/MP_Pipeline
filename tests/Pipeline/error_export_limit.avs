# expect: MP_Pipeline: script block 2 exports 9 clips, max. 8 are allowed
# a process can export at most 8 clips besides its result (8 is fine, see formats_a)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
MP_Pipeline("""
ColorBars()
### ###
a1 = last
a2 = last
a3 = last
a4 = last
a5 = last
a6 = last
a7 = last
a8 = last
a9 = last
### export clip: a1, a2, a3, a4, a5, a6, a7, a8, a9
### ###
""")
