# "### audio: off": the block passes its clip downstream without audio
# generated settings: MPP_DLL, LOG_DIR, OTHER_PLATFORM (see run_tests.ps1)
Import(ScriptDir() + "_config.avsi")
LoadPlugin(MPP_DLL)
Import(ScriptDir() + "audio_common.avsi")
c = AudioPipeline(AudioImport() + """
AudioSource()
### audio: off
### ###
""")
Assert(AudioIsPlain() || !HasAudio(c), "audio_off: the output has audio")
# both runs (plain, MP_Pipeline) give "Audio: none"
c.KillAudio()
