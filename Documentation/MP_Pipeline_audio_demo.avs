# MP_Pipeline audio demo (AviSynth+ 3.7 or later, MP_Pipeline 2.1 or later)
#
# The audio travels with the clips from block to block, like the video frames, also
# through export clip, prefetch and branch. Play the script to hear the result; Info()
# in the main process shows the audio format that arrived.

LoadPlugin("MP_Pipeline.dll") # can be omitted if put into an autoloaded directory
MP_Pipeline("""

# Block 1: source with audio. ColorBars has a 440 Hz test tone (stereo, 48 kHz, float).
# For a real file use a source filter with audio, e.g. AviSource("movie.avi").
ColorBars(width=640, height=360, pixel_type="YV12").Trim(0, 299)
# a second clip with its own audio, exported to block 2
quiet = Amplify(0.25)
### export clip: quiet
### prefetch: 8, 0
### ###

# Block 2: audio filters work on the clips coming from block 1. Add "### platform: win32"
# (or win64) here to run the block in the other bitness; the audio is passed there too.
Blur(1.0)
DelayAudio(0.5)
MixAudio(last, quiet, 0.8, 0.2)
ConvertAudioTo16bit()
### prefetch: 8, 0
### ###

# Block 3: two branch processes share the video frames. Each branch has the whole audio,
# downstream it comes from the first branch process.
Sharpen(0.5)
### branch: 2
### ###

# Main process: Info() shows the audio that arrived (16 bit, 2 channels, 48000 Hz).
# Add "### audio: off" to a block above: its clips, and so this one, arrive without audio.
Info()

""")
