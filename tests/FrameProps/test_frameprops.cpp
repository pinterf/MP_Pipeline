// Round-trip and robustness test for AVS_SplitProcess/FrameProps.cpp.
// Not part of the plugin build, see build.bat. Needs AviSynth+ with interface V11+ (avisynth.dll on PATH).
//
// Set FUZZ_DUMP=1 to write each corrupted fuzz input to last_fuzz.bin before it is parsed,
// so the input of a crash can be inspected afterwards.

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "FrameProps.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <random>
#include <limits>
#include <stdexcept>

const AVS_Linkage* AVS_linkage = nullptr;
typedef IScriptEnvironment* (__stdcall* CreateEnv_t)(int);

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// serialized blocks must start 8 byte aligned (as they do in shared memory)
struct AlignedBuf {
  std::vector<uint64_t> v;
  uint8_t* p() { return (uint8_t*)v.data(); }
  void resize(size_t n) { v.assign((n + 7) / 8 + 1, 0); }
};

static bool transferable(char t) { return t == 'i' || t == 'f' || t == 's'; }

static int count_transferable(IScriptEnvironment* env, const AVSMap* m) {
  int n = 0;
  for (int k = 0; k < env->propNumKeys(m); k++)
    if (transferable(env->propGetType(m, env->propGetKey(m, k)))) n++;
  return n;
}

// every transferable key of a must be in b with identical content; hints_kept=false -> b's hints must be unknown
static void compare_maps(IScriptEnvironment* env, const AVSMap* a, const AVSMap* b, bool hints_kept, const char* label) {
  CHECK(count_transferable(env, a) == env->propNumKeys(b), "%s: key count %d vs %d", label, count_transferable(env, a), env->propNumKeys(b));
  for (int k = 0; k < env->propNumKeys(a); k++) {
    const char* key = env->propGetKey(a, k);
    char t = env->propGetType(a, key);
    if (!transferable(t)) { CHECK(env->propGetType(b, key) == 'u', "%s: skipped key %s present", label, key); continue; }
    CHECK(env->propGetType(b, key) == t, "%s: type of %s", label, key);
    int n = env->propNumElements(a, key);
    CHECK(env->propNumElements(b, key) == n, "%s: count of %s %d vs %d", label, key, n, env->propNumElements(b, key));
    if (env->propNumElements(b, key) != n) continue;
    int e = 0;
    for (int i = 0; i < n; i++) {
      if (t == 'i') CHECK(env->propGetInt(a, key, i, &e) == env->propGetInt(b, key, i, &e), "%s: %s[%d]", label, key, i);
      if (t == 'f') {
        double x = env->propGetFloat(a, key, i, &e), y = env->propGetFloat(b, key, i, &e);
        CHECK(memcmp(&x, &y, 8) == 0, "%s: %s[%d] %g vs %g", label, key, i, x, y); // bitwise: NaN, -0.0
      }
      if (t == 's') {
        int sa = env->propGetDataSize(a, key, i, &e), sb = env->propGetDataSize(b, key, i, &e);
        CHECK(sa == sb && memcmp(env->propGetData(a, key, i, &e), env->propGetData(b, key, i, &e), sa) == 0, "%s: data %s[%d]", label, key, i);
        int ha = env->propGetDataTypeHint(a, key, i, &e), hb = env->propGetDataTypeHint(b, key, i, &e);
        CHECK(hints_kept ? ha == hb : hb == PROPDATATYPEHINT_UNKNOWN, "%s: hint %s[%d] %d vs %d", label, key, i, ha, hb);
      }
    }
  }
}

static int run() {
  HMODULE dll = LoadLibraryA("avisynth.dll");
  if (!dll) { printf("avisynth.dll not found\n"); return 2; }
  IScriptEnvironment* env = ((CreateEnv_t)GetProcAddress(dll, "CreateScriptEnvironment"))(AVISYNTH_CLASSIC_INTERFACE_VERSION);
  if (!env) { printf("CreateScriptEnvironment failed\n"); return 2; }
  AVS_linkage = env->GetAVSLinkage();
  {
    FramePropsCaps caps = detect_frame_props_caps(env);
    printf("avs: %s | caps: props=%d hint=%d | %d-bit\n", env->Invoke("VersionString", AVSValue(nullptr, 0)).AsString(),
      caps.has_props, caps.has_type_hint, (int)sizeof(void*) * 8);
    if (!caps.has_props || !caps.has_type_hint) { printf("needs AviSynth+ interface V11+\n"); return 2; }

    PClip some_clip = env->Invoke("BlankClip", AVSValue(nullptr, 0)).AsClip();
    VideoInfo vi = some_clip->GetVideoInfo();
    PVideoFrame src = env->NewVideoFrame(vi);
    AVSMap* m = env->getFramePropsRW(src);

    CHECK(serialize_frame_props(env, caps, src, nullptr, 0) == 0, "empty map should give 0");

    // --- source map with every supported shape ---
    env->propSetInt(m, "_Matrix", 1, PROPAPPENDMODE_REPLACE);
    env->propSetInt(m, "_ColorRange", 1, PROPAPPENDMODE_REPLACE); // stored as _Range by newer cores
    int64_t ints[] = { 0, -1, (std::numeric_limits<int64_t>::max)(), (std::numeric_limits<int64_t>::min)(), 42 };
    env->propSetIntArray(m, "int_arr", ints, 5);
    env->propSetIntArray(m, "int_empty", ints, 0);
    double fl[] = { 3.25, -0.0, std::numeric_limits<double>::infinity(), std::nan(""), 1e-300 };
    env->propSetFloatArray(m, "float_arr", fl, 5);
    env->propSetFloatArray(m, "float_empty", fl, 0);
    env->propSetDataH(m, "str", "hello \xC5\x91", -1, PROPDATATYPEHINT_UTF8, PROPAPPENDMODE_REPLACE);
    const unsigned char bin[] = { 0, 1, 2, 0, 0xFF, 7, 9 }; // odd length -> padding
    env->propSetDataH(m, "bin_multi", (const char*)bin, 7, PROPDATATYPEHINT_BINARY, PROPAPPENDMODE_APPEND);
    env->propSetDataH(m, "bin_multi", "", 0, PROPDATATYPEHINT_BINARY, PROPAPPENDMODE_APPEND);
    env->propSetDataH(m, "bin_multi", "abc", 3, PROPDATATYPEHINT_UNKNOWN, PROPAPPENDMODE_APPEND);
    std::vector<int64_t> mv(1000000);
    for (size_t i = 0; i < mv.size(); i++) mv[i] = (int64_t)(i * 2654435761u) - 12345; // "motion vectors"
    env->propSetIntArray(m, "big_vectors", mv.data(), (int)mv.size());
    std::string k255(255, 'k');
    env->propSetInt(m, k255.c_str(), 7, PROPAPPENDMODE_REPLACE);
    env->propSetClip(m, "a_clip", some_clip, PROPAPPENDMODE_REPLACE);   // must be skipped
    PVideoFrame other = env->NewVideoFrame(vi);
    env->propSetFrame(m, "a_frame", other, PROPAPPENDMODE_REPLACE);      // must be skipped

    // --- measure, write, round trip ---
    size_t need = serialize_frame_props(env, caps, src, nullptr, 0);
    AlignedBuf buf; buf.resize(need);
    size_t wrote = serialize_frame_props(env, caps, src, buf.p(), need);
    CHECK(need == wrote && need > 0, "measure %zu vs write %zu", need, wrote);
    printf("serialized %zu bytes, %d keys (incl. 1M int64 array, 1 clip, 1 frame)\n", need, env->propNumKeys(m));
    uint32_t skipped; memcpy(&skipped, buf.p() + 16, 4);
    CHECK(skipped == 2, "skipped_count %u", skipped);

    bool threw = false;
    AlignedBuf small; small.resize(need);
    try { serialize_frame_props(env, caps, src, small.p(), need - 1); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw, "too small capacity did not throw");

    PVideoFrame dst = env->NewVideoFrame(vi);
    deserialize_frame_props(env, caps, dst, buf.p(), wrote);
    compare_maps(env, env->getFramePropsRO(src), env->getFramePropsRO(dst), true, "roundtrip");
    int e = 0;
    CHECK(env->propGetInt(env->getFramePropsRO(dst), "_ColorRange", 0, &e) == 1 && !e, "_ColorRange alias");

    // pre-V11 reader: hints become unknown, values intact
    FramePropsCaps v8 = { true, false };
    PVideoFrame dst8 = env->NewVideoFrame(vi);
    deserialize_frame_props(env, v8, dst8, buf.p(), wrote);
    compare_maps(env, env->getFramePropsRO(src), env->getFramePropsRO(dst8), false, "v8 reader");

    // no property support (AviSynth 2.6): nothing written, nothing read
    FramePropsCaps none = { false, false };
    CHECK(serialize_frame_props(env, none, src, nullptr, 0) == 0, "no-props serialize");
    PVideoFrame dstn = env->NewVideoFrame(vi);
    deserialize_frame_props(env, none, dstn, buf.p(), wrote);
    CHECK(env->propNumKeys(env->getFramePropsRO(dstn)) == 0, "no-props deserialize");

    // invalid hint in the stream (the core would store it unchecked): becomes unknown
    {
      PVideoFrame h = env->NewVideoFrame(vi);
      env->propSetDataH(env->getFramePropsRW(h), "h", "x", 1, PROPDATATYPEHINT_UTF8, PROPAPPENDMODE_REPLACE);
      AlignedBuf hb; hb.resize(serialize_frame_props(env, caps, h, nullptr, 0));
      size_t hsize = serialize_frame_props(env, caps, h, hb.p(), 4096);
      // header 24, entry head 8, key "h" padded to 8, data length 4, then the hint
      const size_t hint_pos = 24 + 8 + 8 + 4;
      int32_t hint; memcpy(&hint, hb.p() + hint_pos, 4);
      CHECK(hint == PROPDATATYPEHINT_UTF8, "hint offset: found %d", hint);
      for (int32_t bad : { 7, -2, 0x7FFFFFFF }) {
        memcpy(hb.p() + hint_pos, &bad, 4);
        PVideoFrame d = env->NewVideoFrame(vi);
        deserialize_frame_props(env, caps, d, hb.p(), hsize);
        int he = 0;
        int got = env->propGetDataTypeHint(env->getFramePropsRO(d), "h", 0, &he);
        CHECK(!he && got == PROPDATATYPEHINT_UNKNOWN, "invalid hint %d stored as %d", bad, got);
      }
    }

    PVideoFrame onlyclip = env->NewVideoFrame(vi);
    env->propSetClip(env->getFramePropsRW(onlyclip), "c", some_clip, PROPAPPENDMODE_REPLACE);
    CHECK(serialize_frame_props(env, caps, onlyclip, nullptr, 0) == 0, "clip-only map should give 0");

    // --- robustness on a small block ---
    env->propDeleteKey(m, "big_vectors");
    size_t sneed = serialize_frame_props(env, caps, src, nullptr, 0);
    AlignedBuf sb; sb.resize(sneed);
    serialize_frame_props(env, caps, src, sb.p(), sneed);

    // every truncation must be rejected
    int trunc_ok = 0;
    for (size_t len = 1; len < sneed; len++) {
      PVideoFrame d = env->NewVideoFrame(vi);
      try { deserialize_frame_props(env, caps, d, sb.p(), len); } catch (const std::runtime_error&) { trunc_ok++; }
    }
    CHECK(trunc_ok == (int)sneed - 1, "truncation: %d of %zu rejected", trunc_ok, sneed - 1);

    // random corruption must never crash
    const bool dump = getenv("FUZZ_DUMP") != nullptr;
    std::mt19937 rng(12345);
    int fuzz_threw = 0, fuzz_ok = 0;
    AlignedBuf fb; fb.resize(sneed);
    for (int it = 0; it < 200000; it++) {
      memcpy(fb.p(), sb.p(), sneed);
      int flips = 1 + rng() % 4;
      for (int f = 0; f < flips; f++) fb.p()[rng() % sneed] ^= (uint8_t)(1 + rng() % 255);
      if (dump) { FILE* df = fopen("last_fuzz.bin", "wb"); fwrite(fb.p(), 1, sneed, df); fclose(df); }
      PVideoFrame d = env->NewVideoFrame(vi);
      try { deserialize_frame_props(env, caps, d, fb.p(), sneed); fuzz_ok++; } catch (const std::runtime_error&) { fuzz_threw++; }
    }
    printf("fuzz: %d rejected, %d accepted (value-only changes), no crash\n", fuzz_threw, fuzz_ok);
  }
  env->DeleteScriptEnvironment();
  printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: OK\n", failures);
  return failures ? 1 : 0;
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  try { return run(); }
  catch (const AvisynthError& e) { printf("AvisynthError: %s\n", e.msg); }
  catch (const std::exception& e) { printf("exception: %s\n", e.what()); }
  catch (...) { printf("unknown exception\n"); }
  return 3;
}
