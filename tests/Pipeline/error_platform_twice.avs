# expect: MP_Pipeline: script block 2 has more than one ### platform statement
# only one ### platform statement per block (the first one would win silently)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
MP_Pipeline("""
### platform: win32
ColorBars()
### ###
### platform: win32
Invert()
### platform: win64
### ###
""")
