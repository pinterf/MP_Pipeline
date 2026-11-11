# slave of the other bitness (x64 master: win32 slave, x86 master: win64 slave)
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
Import(ScriptDir() + "props_common.avsi")
MP_Pipeline(PropsImport() + """
PropsSource()
### platform: """ + OTHER_PLATFORM + """
### ###
Invert()
### ###
""")
PropsVerify("cross_bitness")
