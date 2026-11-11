# Shared helpers of the frame property pipeline tests, imported by the master script
# and (via PropsImport) by the slave processes.

global PROPS_FRAMES = 800
# Phases, exercising the overflow mapping policy of each response slot:
#   0   .. 299  all variants incl. 5 MB      -> overflow mapping grows (64 KB, then ~7.5 MB)
#   300 .. 449  only 100 KB and 5 KB          -> shrinks (peak < 1/4 capacity)
#   450 .. 799  only inline sized properties  -> overflow mapping released (needs a
#               full window, PROPS_SHRINK_WINDOW = 64 writes per slot, without overflow)
global PROPS_SHRINK_START = 300
global PROPS_TAIL_START = 450

# string of exactly 'size' bytes, first and last character is 'mark'
function PropsBlob(int size, string mark)
{
  s = "0123456789abcdef"
  while (StrLen(s) < size) { s = s + s }
  return mark + LeftStr(s, size - 2) + mark
}

# which blob variant a source frame carries
function PropsVariant(int n)
{
  return n >= PROPS_TAIL_START ? 1 : \
         n >= PROPS_SHRINK_START ? ((n / 7) % 2 == 0 ? 5 : 3) : \
         (n / 7) % 6
}
function PropsVariantSize(int v) { return Select(v, 0, 100, 4000, 5000, 5000000, 100000) }
function PropsVariantMark(int v) { return Select(v, "", "a", "b", "c", "d", "e") }

# source with per frame properties of varying size (runs in the first slave)
#   fn:       source frame number
#   i_arr, f_arr, s_utf8: fixed values of each type
#   blob:     data property, size by PropsVariant: none / inline / near inline limit /
#             small overflow / 5 MB / 100 KB
function PropsSource()
{
  base = ColorBars(width=320, height=240, pixel_type="YV12").Trim(0, PROPS_FRAMES - 1)
  base = base.propSet("fn", function[]() { current_frame })
  # propSetArray: the debug core asserts on propSet(name, array) (reported to AviSynth+, release builds unaffected)
  base = base.propSetArray("i_arr", function[]() { [1, -2, 300] }).propSetArray("f_arr", function[]() { [0.5, -1.25] }).propSet("s_utf8", "MP_Pipeline")
  c0 = base
  c1 = base.propSet("blob", PropsBlob(PropsVariantSize(1), PropsVariantMark(1)))
  c2 = base.propSet("blob", PropsBlob(PropsVariantSize(2), PropsVariantMark(2)))
  c3 = base.propSet("blob", PropsBlob(PropsVariantSize(3), PropsVariantMark(3)))
  c4 = base.propSet("blob", PropsBlob(PropsVariantSize(4), PropsVariantMark(4)))
  c5 = base.propSet("blob", PropsBlob(PropsVariantSize(5), PropsVariantMark(5)))
  return ConditionalSelect(base, function[]() { PropsVariant(current_frame) }, c0, c1, c2, c3, c4, c5)
}

# script line importing this file, to be put in front of a slave block
function PropsImport()
{
  return "Import(" + Chr(34) + ScriptDir() + "props_common.avsi" + Chr(34) + ")" + Chr(10)
}

# source frame number of output frame n after the reordering done by PropsVerify
function PropsSrcFrame(int n, string order)
{
  return order == "reverse" ? PROPS_FRAMES - 1 - n : \
         order == "shuffle" ? (n / 10) * 10 + Select(n % 10, 3, 1, 4, 0, 9, 2, 6, 5, 8, 7) : \
         n
}

function PropsCheckFrame(clip c, int n, string order)
{
  src = PropsSrcFrame(n, order)
  v = PropsVariant(src)
  size = PropsVariantSize(v)
  mark = PropsVariantMark(v)
  fn = propGetType(c, "fn") == 0 ? -1 : propGetInt(c, "fn")
  s = propGetType(c, "blob") == 0 ? "" : propGetString(c, "blob")
  ia = propGetType(c, "i_arr") == 0 ? [0, 0, 0] : propGetAsArray(c, "i_arr")
  fa = propGetType(c, "f_arr") == 0 ? [0.0, 0.0] : propGetAsArray(c, "f_arr")
  fixed_ok = ArraySize(ia) == 3 && ia[0] == 1 && ia[1] == -2 && ia[2] == 300 \
    && ArraySize(fa) == 2 && fa[0] == 0.5 && fa[1] == -1.25 \
    && propGetType(c, "s_utf8") != 0 && propGetString(c, "s_utf8") == "MP_Pipeline"
  blob_ok = StrLen(s) == size && (size == 0 || (LeftStr(s, 1) == mark && RightStr(s, 1) == mark))
  ok = fn == src && fixed_ok && blob_ok
  return String(n) + (ok ? " OK" : " FAIL src=" + String(src) + " fn=" + String(fn) + " blob_len=" + String(StrLen(s)) \
    + " expected_len=" + String(size) + " fixed_ok=" + String(fixed_ok))
}

# reorders the pipeline output (order: "linear", "reverse", "shuffle") and writes one
# check line per frame into LOG_DIR + name + ".log"
function PropsVerify(clip c, string name, string "order")
{
  order = Default(order, "linear")
  c = order == "reverse" ? c.Reverse() : \
      order == "shuffle" ? c.SelectEvery(10, 3, 1, 4, 0, 9, 2, 6, 5, 8, 7) : \
      c
  return c.WriteFile(LOG_DIR + name + ".log", "PropsCheckFrame(last, current_frame, " + Chr(34) + order + Chr(34) + ")")
}
