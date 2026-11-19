# audio from a slave of the other bitness (x64 master: win32 slave, x86 master: win64 slave)
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
Import(ScriptDir() + "audio_common.avsi")
AudioPipeline(AudioImport() + """
AudioSource()
### platform: """ + OTHER_PLATFORM + """
### ###
""" + AudioImport() + """AudioDub(last, AudioSource().GetChannel(2).Amplify(0.7).ConvertAudioTo16bit().AssumeSampleRate(44100))
### platform: """ + OTHER_PLATFORM + """
### ###
""")
Assert(HasAudio(last) && AudioRate(last) == 44100, "audio_cross: expected the 44.1 kHz audio of AudioDub")
last
