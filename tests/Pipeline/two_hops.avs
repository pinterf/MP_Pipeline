# properties pass two slave processes (the second one processes the pixels)
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
Import(ScriptDir() + "props_common.avsi")
MP_Pipeline(PropsImport() + """
PropsSource()
### ###
Invert()
### ###
""")
PropsVerify("two_hops")
