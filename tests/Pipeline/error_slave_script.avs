# expect: Script error: I don't know what 'undefined_variable' means
# A failing slave script: its error message must reach the master (was empty in 0.23)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
MP_Pipeline("""
ColorBars()
Subtitle(undefined_variable)
### ###
""")
