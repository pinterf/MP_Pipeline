# Pixel format transfer test helpers (AviSynth+ with the interface V13 formats), imported by
# the master script and (via FormatsImport) by the slave process.

global FORMATS_FRAMES = 800
global FORMATS_COUNT = 16 # FormatsMake variants, 8 per scenario

# YUVA444P16 source, every plane has its own content, alpha too; changes per frame.
# Width 344: plane rows are not multiples of the 64 byte frame alignment.
function FormatsBase()
{
  c = ColorBars(width=344, height=96, pixel_type="YUV444P16").Trim(0, FORMATS_FRAMES - 1)
  c = ScriptClip(c, function[](clip c) { c.Subtitle(String(current_frame), size=40, x=current_frame % 300) })
  return c.AddAlphaPlane(c.ExtractY().FlipVertical())
}

# test format i (FORMATS_COUNT formats), made from FormatsBase(); the conversions keep the
# alpha of the source, n has none
#   0..2   YA8, YA16, YAS          3..5   YUV440, YUV440P10, YUVA440P16
#   6..9   YV411, YUV411P12, YUV411PS, YUVA411P10
#   10..13 YUV9, YUV410P16, YUVA410, YUVA410PS
#   14, 15 YUVA444P16, Y10
function FormatsMake(clip b, int i)
{
  n = b.RemoveAlphaPlane()
  return Select(i, \
    b.ConvertToYA(bits=8), \
    b.ConvertToYA(bits=16), \
    b.ConvertToYA(bits=32), \
    n.ConvertToYUV440(bits=8), \
    n.ConvertToYUV440(bits=10), \
    b.ConvertToYUVA440(bits=16), \
    n.ConvertToYUV411(bits=8), \
    n.ConvertToYUV411(bits=12), \
    n.ConvertToYUV411(bits=32), \
    b.ConvertToYUVA411(bits=10), \
    n.ConvertToYUV410(bits=8), \
    n.ConvertToYUV410(bits=16), \
    b.ConvertToYUVA410(bits=8), \
    b.ConvertToYUVA410(bits=32), \
    b, \
    n.ConvertToY().ConvertBits(10))
}

# script line importing this file, to be put in front of a slave block
function FormatsImport()
{
  return "Import(" + Chr(34) + ScriptDir() + "formats_common.avsi" + Chr(34) + ")" + Chr(10)
}

# planes of c as Y clips: Y; Y A; Y U V; Y U V A
function FormatsPlanes(clip c)
{
  n = NumComponents(c)
  return n == 1 ? [c] : \
         n == 2 ? [c.ExtractY(), c.ExtractA()] : \
         n == 3 ? [c.ExtractY(), c.ExtractU(), c.ExtractV()] : \
                  [c.ExtractY(), c.ExtractU(), c.ExtractV(), c.ExtractA()]
}

# one line per frame: "<n> OK" when every plane of every transferred clip equals the local one
function FormatsCheckFrame(int n)
{
  bad = ""
  for (k = 0, ArraySize(FORMATS_REMOTE) - 1) {
    if (LumaDifference(FORMATS_REMOTE[k], FORMATS_LOCAL[k]) != 0.0) { bad = bad + " " + FORMATS_LABEL[k] }
  }
  return String(n) + (bad == "" ? " OK" : " FAIL" + bad)
}

# Runs formats first .. first+7 through one slave (a process exports at most 8 clips) and
# returns the checking clip, writing LOG_DIR + name + ".log"; platform: of the slave (default: same)
function FormatsRun(int first, string name, string "platform")
{
  script = (Defined(platform) ? "### platform: " + platform + Chr(10) : "") + FormatsImport() + "b = FormatsBase()" + Chr(10)
  exports = ""
  for (k = 0, 7) {
    script = script + "f" + String(k) + " = FormatsMake(b, " + String(first + k) + ")" + Chr(10)
    exports = exports + (k > 0 ? ", " : "") + "f" + String(k)
  }
  script = script + "f0" + Chr(10) + "### export clip: " + exports + Chr(10) + "### ###" + Chr(10)
  # main block: a block result must be a clip, the received clips go into a global
  script = script + "global FORMATS_RECEIVED = [" + exports + "]" + Chr(10) + "f0" + Chr(10)
  received = MP_Pipeline(script)
  return FormatsVerify(received, FORMATS_RECEIVED, first, name)
}

# remote: the 8 clips received from the slave, formats first .. first+7
# pipeline: the MP_Pipeline clip, the result is made from it to keep the pipeline alive
function FormatsVerify(clip pipeline, val remote, int first, string name)
{
  b = FormatsBase()
  r = []
  l = []
  lab = []
  for (i = 0, 7) {
    rc = remote[i]
    lc = FormatsMake(b, first + i)
    Assert(PixelType(rc) == PixelType(lc), "format " + String(first + i) + ": received " + PixelType(rc) + ", expected " + PixelType(lc))
    rp = FormatsPlanes(rc)
    lp = FormatsPlanes(lc)
    for (p = 0, ArraySize(rp) - 1) {
      r = ArrayAdd(r, rp[p])
      l = ArrayAdd(l, lp[p])
      lab = ArrayAdd(lab, PixelType(lc) + "/" + String(p))
    }
  }
  global FORMATS_REMOTE = r
  global FORMATS_LOCAL = l
  global FORMATS_LABEL = lab
  return pipeline.ConvertToY().ConvertBits(8).WriteFile(LOG_DIR + name + ".log", "FormatsCheckFrame(current_frame)")
}
