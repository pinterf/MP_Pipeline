# audio with branch processes: ThunkedInterleave passes the audio of the first branch
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
Import(ScriptDir() + "audio_common.avsi")
AudioPipeline(AudioImport() + """
AudioSource()
### prefetch: 8, 4
### branch: 3, 4
Blur(1.0)
### ###
""")
Assert(HasAudio(last), "audio_branch: the output has no audio")
last
