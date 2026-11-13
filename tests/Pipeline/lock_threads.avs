# "### lock threads to cores": accepted and ignored since 2.0 (no thread pinning any more);
# scripts using it must still work, frames and properties pass unchanged (prefetch, branch, a plain hop)
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
Import(ScriptDir() + "props_common.avsi")
MP_Pipeline(PropsImport() + """
PropsSource()
### lock threads to cores
### prefetch: 16, 4
### ###
### lock threads to cores
### branch: 2, 4
### ###
Invert()
### lock threads to cores
### ###
""")
PropsVerify("lock_threads")