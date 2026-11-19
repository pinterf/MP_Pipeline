# audio through serial blocks with prefetch, the host block shifts the audio (requests before 0)
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
Import(ScriptDir() + "audio_common.avsi")
AudioPipeline(AudioImport() + """
AudioSource()
### prefetch: 4, 2
### ###
Blur(1.0)
Amplify(0.8, 0.6)
### ###
DelayAudio(0.1)
""")
Assert(HasAudio(last), "audio_blocks: the output has no audio")
last
