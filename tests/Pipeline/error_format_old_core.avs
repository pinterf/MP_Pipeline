# expect: needs AviSynth+ interface V13 or later in this process
# a master without the V13 formats receives YA8 from a slave of the other bitness (which must have them)
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
Assert(!FunctionExists("ConvertToYUV440"), "SKIP: the master has the V13 formats (runs with an older master core only)")
LoadPlugin(MPP_DLL)
MP_Pipeline("""
### platform: """ + OTHER_PLATFORM + """
ColorBars(width=320, height=240, pixel_type="YUV444P8").ConvertToYA(bits=8)
### ###
""")
