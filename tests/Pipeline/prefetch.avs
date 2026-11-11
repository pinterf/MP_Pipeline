# server side prefetch: properties must stay with their frames
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
Import(ScriptDir() + "props_common.avsi")
MP_Pipeline(PropsImport() + """
PropsSource()
### prefetch: 16, 4
### ###
""")
PropsVerify("prefetch")
