# a clip exported by the first slave and passed through the second one (aux clip of both servers)
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
Import(ScriptDir() + "props_common.avsi")
MP_Pipeline(PropsImport() + """
a = PropsSource()
a.Invert()
### export clip: a
### ###
Invert()
### pass clip: a
### ###
StackHorizontal(last, PropsVerify(a, "pass_clip"))
""")
