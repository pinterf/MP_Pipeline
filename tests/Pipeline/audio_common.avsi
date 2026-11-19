# Helpers of the audio scenarios (audio_*.avs). They run with the test host avs_run -audio, which
# reads the audio of each frame and the whole audio in large requests and prints a hash of both;
# run_tests.ps1 / run_tests.sh compare it with a run of avs_run -audio -plain.
# -plain sets MPP_PLAIN = true: the blocks then run in this script without MP_Pipeline (the
# "###" statement lines are comments), giving the reference audio.

function AudioIsPlain()
{
  plain = false
  try { plain = MPP_PLAIN } catch (err) { plain = false }
  return plain
}

function AudioPipeline(string script)
{
  return AudioIsPlain() ? Eval(script) : MP_Pipeline(script)
}

# 300 frames of video with 48 kHz stereo float audio: the ColorBars tone, the right channel mixed
# with a delayed copy, so that the two channels differ. No Tone(): its samples depend on the size
# of the GetAudio requests (float rounding), so a run reading in other chunks than the reference
# would differ; ColorBars, MixAudio, Amplify and DelayAudio give the same samples for any request.
function AudioSource()
{
  v = ColorBars(width=320, height=240, pixel_type="YV12").Trim(0, 299)
  right = MixAudio(v, v.DelayAudio(0.0037), 0.5, 0.5).GetChannel(2)
  return AudioDub(v, MergeChannels(v.GetChannel(1), right))
}

# script line importing this file, to be put in front of a slave block
function AudioImport()
{
  return "Import(" + Chr(34) + ScriptDir() + "audio_common.avsi" + Chr(34) + ")" + Chr(10)
}
