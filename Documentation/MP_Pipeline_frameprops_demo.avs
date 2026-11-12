# MP_Pipeline frame property demo (AviSynth+ 3.7 or later)
#
# Properties set in a slave process arrive in the downstream processes and in the main
# process. propShow() at the end lists what arrived.

LoadPlugin("MP_Pipeline.dll") # can be omitted if put into an autoloaded directory
MP_Pipeline("""

# Block 1: source, sets properties
ColorBars(width=640, height=360, pixel_type="YV12").Trim(0, 299)
propSet("_Matrix", 1) # BT.709
# per frame value: current_frame is set by runtime filters like ScriptClip
ScriptClip(function[](clip c) { c.propSet("source_frame", current_frame) })
propSet("note", "set in block 1")
# a clip exported to block 2, with its own properties
logo = BlankClip(last, color=$202020).propSet("note", "logo from block 1")
### export clip: logo
### ###

# Block 2: processes the pixels, the properties of block 1 are already here
Overlay(last, logo, opacity=0.5)
propSet("blurred", 1)
Blur(1.0)
### prefetch: 4, 0
### ###

# Block 3: debug helper, its own property is not passed on
propSet("only_in_block_3", 1)
### frame props: off
### ###

# Main process: no property arrives here, because block 3 has "### frame props: off".
# Remove that line to see _Matrix, source_frame, note, blurred and only_in_block_3
# listed by propShow.
propShow()

""")
