# three branch processes (thunked interleave) with non-linear access
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
Import(ScriptDir() + "props_common.avsi")
MP_Pipeline(PropsImport() + """
PropsSource()
### branch: 3, 4
### ###
""")
PropsVerify("branch_shuffle", "shuffle")
