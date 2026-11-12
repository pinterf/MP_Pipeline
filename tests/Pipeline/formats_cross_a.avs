# pixel formats passed from a slave of the other bitness (OTHER_PLATFORM), compared plane by plane with locally made ones
# formats 0..7: YA 8/16/32 bit, 4:4:0 8/10 bit and 16 bit with alpha, 4:1:1 8/12 bit
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
Assert(FunctionExists("ConvertToYUV440"), "SKIP: needs AviSynth+ with the interface V13 pixel formats")
LoadPlugin(MPP_DLL)
Import(ScriptDir() + "formats_common.avsi")
FormatsRun(0, "formats_cross_a", OTHER_PLATFORM)
