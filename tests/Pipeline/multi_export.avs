# two clips exported from one slave process (aux clip of the server)
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
Import(ScriptDir() + "props_common.avsi")
MP_Pipeline(PropsImport() + """
a = PropsSource()
b = a.Invert()
# the server exports "last" as clip 0, a and b as aux clips
a
### export clip: a, b
### ###
StackHorizontal(PropsVerify(a, "multi_export_a"), PropsVerify(b, "multi_export_b"))
""")
