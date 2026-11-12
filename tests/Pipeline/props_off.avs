# "### frame props: off" in the first block: its frames leave the process without properties,
# the second block ("on" is the default, given explicitly here) has none to pass on
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
Import(ScriptDir() + "props_common.avsi")
MP_Pipeline(PropsImport() + """
PropsSource()
### frame props: off
### ###
Invert()
### frame props: on
### ###
""")
PropsVerify("props_off", "none")
