"""

Usage example:

"""

LoadPlugin("MP_Pipeline.dll") # can be omitted if put into an autoloaded directory
SetMemoryMax(1) # though avs+ does not allow that low, this allows the lowest possible
MP_Pipeline("""

### platform: win64
# this part of script will be run in a 64 bit process

### dll: avisynth.dll
# you can use a different version of avisynth to run script block, just specify path to the avisynth dll like the line above

BlankClip()
ConvertToYV12()

# The line below is splitter
### ###

### platform: win32

### inherit start ###

# Codes in this block will be copied to all downstream processses (including the main process),
# so you can add LoadPlugin() calls here.

### inherit end ###

ScriptClip("Subtitle(string(current_frame))")
AddGrainC(1.0)


### ###


### platform: win32

a = ColorBars().ConvertToYV12().Subtitle("This is clip A, branch ID = " + string(MP_PIPELINE_BRANCH_ID)) # .ScriptClip("Subtitle(string(current_frame), y=20)")
b = ColorBars().ConvertToYV12().Subtitle("This is clip B, branch ID = " + string(MP_PIPELINE_BRANCH_ID)) # .ScriptClip("Subtitle(string(current_frame), y=20)")

tnlmeans

Subtitle(string(MP_PIPELINE_BRANCH_ID), y=30)


### export clip: a, b
# Export some clip variables to the next downstream process.
# The variables are only available in the next process, unless you add a PASS CLIP statement (see below)

# This part will run parallelly in 4 processes, specified by BRANCH statement below.
### branch: 4, 3

# SOME OTHER FILTERS # <-- Note: Lines after branch statement will be added after SelectEvery

### prefetch: 5, 2
# Prefetch max. 5 frames in this process, 2 frames behind currently requested frame will be kept to allow backward seeking
# All cache frames will be purged if we seek more than 2 frames backward

### ###

StackHorizontal(last, a)

### pass clip: b
# Clip b will also be available in the next process.
# Note that b in the next process is directly imported from the upstream process, that means any changes to this variable
# in current process won't be visible in next process

### prefetch: 5, 2

### ###


StackHorizontal(last, b)

### prefetch: 5, 2

### ###

# Codes here will be run in the main process, all statements (branch, prefetch etc.) in this block will be ignored
last

""")

"""

Note: This example script is used for demostration only. It is far from optimal. You need to tweak the settings yourself for best speed.

Available statements:

### branch: <number of branch processes>[, <thunk size>]

Parallelize script processing. See example above for details. 

Note that this statement is only suitable for spatial filters. For temporal filters, the processing speed may be slower than single process due to filter mechanics.

Thunking:

You can specify thunk size in the branch statement. For example if you write "### branch: 4, 4", slave processes will process frames like this:

Process 0:  0,  1,  2,  3, 16, 17, 18, 19, ...
Process 1:  4,  5,  6,  7, 20, 21, 22, 23, ...
Process 2:  8,  9, 10, 11, 24, 25, 26, 27, ...
Process 3: 12, 13, 14, 15, 28, 29, 30, 31, ...

If thunk size is set to 1 or omitted, it will behave like using regular SelectEvery and Interleave.

#######################################################

### platform: <win32|win64>

Run the containing part of script on specified platform, so that you can use 32bit filter in 64bit main process, or vice vesa.

#######################################################

### prefetch: <max frames in cache per clip>, <frames to keep behind current frame>

Enable prefetching. Please see example above for details.

Note: Don't enable prefetching in processes that require random access (such as processes that are upstream of branched process) unless you know what you are doing, otherwise performance may be significantly degraded.

#######################################################

### export clip: <one or more clip variables separated by comma>

Please see the example above for details.

#######################################################

### pass clip: <one or more clip variables separated by comma>

Please see the example above for details.

#######################################################

### lock threads to cores

Slave processes with this statement will lock their worker threads into a core. This may improve performance for some scripts.

Core selection is on a round-robin manner, with awareness of hyper-threading.

#######################################################

### inherit start ### / ### inherit end ###

Duplicate codes into downstream processes.

#######################################################

### frame props: <on|off>

Default is on: frame properties of the clips leaving the process are passed downstream.
With "off" the frames of this process arrive downstream without any frame property.
Use it when the properties are not needed and passing them is costly (e.g. large
string/data properties), or for comparison when something seems wrong.

#######################################################

Frame properties (AviSynth+)

Frame properties travel with the frames between the processes, in both directions of
bitness (win32 <-> win64), through branch, prefetch, export clip and pass clip.

- Requires AviSynth+ with frame property support (interface version 8 or later) in both
  processes. If either side has no frame properties (e.g. AviSynth 2.6), nothing is passed.
- Passed: integer, float and string/data properties, including arrays.
  The binary/UTF-8 type hint of data properties is kept when both cores support it
  (interface version 11 or later).
- Not passed: clip-typed and frame-typed properties. They cannot cross process boundaries
  and are silently skipped.
- Size: up to 4 KB of property data per frame travels together with the frame. Larger
  property sets use an extra shared memory buffer, which grows on demand, shrinks and is
  released again when the large properties are gone (checked every 64 frames of a buffer).
  The limit is 256 MB of property data per frame.

Diagnostics: with the environment variable MPP_TRACE_PROPS=1 set before the host starts,
both sides report the creation, resizing and release of the extra buffers with
OutputDebugString (visible e.g. in DebugView), lines start with "MPP_Props".

See MP_Pipeline_frameprops_demo.avs for an example.

"""

trim(0, 100)


