# expect: slaves of another platform are supported on Windows only
# "### platform:" where there is no other platform (Linux): a clear error, not a missing file
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM, PLATFORMS_SUPPORTED (see run_tests.ps1 / .sh)
Import(ScriptDir() + "_config.avsi")
Assert(!PLATFORMS_SUPPORTED, "SKIP: other platforms are supported here")
LoadPlugin(MPP_DLL)
MP_Pipeline("""
### platform: win32
ColorBars()
### ###
""")
