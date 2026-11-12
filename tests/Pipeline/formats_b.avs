# pixel formats passed from a slave, compared plane by plane with locally made ones
# formats 8..15: 4:1:1 float and 10 bit with alpha, 4:1:0 8/16 bit, 8 bit and float with alpha, YUVA444P16, Y10
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
Assert(FunctionExists("ConvertToYUV440"), "SKIP: needs AviSynth+ with the interface V13 pixel formats")
LoadPlugin(MPP_DLL)
Import(ScriptDir() + "formats_common.avsi")
FormatsRun(8, "formats_b")
