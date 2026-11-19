# audio sample types and channel counts, also in exported clips (aux clips of the server)
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
Import(ScriptDir() + "audio_common.avsi")
AudioPipeline(AudioImport() + """
s = AudioSource()
a8 = s.ConvertAudioTo8bit().GetChannel(1)
a24 = s.Amplify(0.5).ConvertAudioTo24bit().GetChannel(2)
a32 = s.Amplify(0.25).ConvertAudioTo32bit()
# 6 channels, 16 bit
s.ConvertAudioTo16bit().GetChannel(1, 2, 1, 2, 2, 1)
### export clip: a8, a24, a32
### ###
MergeChannels(last.ConvertAudioToFloat(), a8.ConvertAudioToFloat(), a24.ConvertAudioToFloat(), a32.ConvertAudioToFloat())
### ###
""")
Assert(HasAudio(last) && AudioChannels(last) == 10, "audio_formats: expected 10 audio channels")
last
