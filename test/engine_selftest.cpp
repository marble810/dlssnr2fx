#include "../engine/dlssnr2fx.h"
#include "../engine/color_adapt.h"
#include "../engine/composite.h"
#include "../engine/eval_identity.h"
#include "../engine/frame_guidance.h"
#include "../engine/mvec.h"
#include "../engine/session_policy.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {
using EvalFn = nr_status(NR_CALL*)(const nr_eval_desc*);
using CancelFn = void(NR_CALL*)(uint32_t, uint32_t);
using ShutdownFn = void(NR_CALL*)();

int Fail(const char* what) {
  std::fprintf(stderr, "FAIL: %s\n", what);
  return 1;
}

std::wstring FullPath(const wchar_t* relative) {
  wchar_t path[MAX_PATH]{};
  GetFullPathNameW(relative, MAX_PATH, path, nullptr);
  return path;
}

bool Touch(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  CloseHandle(file);
  return true;
}

nr_eval_desc MakeDesc(const std::wstring& runtime, const std::vector<uint8_t>& input,
                      std::vector<uint8_t>& output) {
  nr_eval_desc d{};
  d.width = 2; d.height = 2; d.adapter = -1;
  d.instance_id = 100; d.request_id = 1; d.timeout_ms = 500;
  d.model.preset = 0; d.model.style = 0; d.model.intensity = 1.0f;
  d.model.local_structure = 1.0f; d.model.local_tone = 1.0f;
  d.model.skin = -1.0f; d.model.global_tone = -1.0f;
  d.detail = 1.0f; d.color = 1.0f;
  d.runtime_dir = runtime.c_str(); d.in_rgba = input.data(); d.out_rgba = output.data();
  return d;
}
}  // namespace

int main() {
  HMODULE dll = LoadLibraryW(L"dist\\engine\\dlssnr2fx.dll");
  if (!dll) return Fail("LoadLibrary dist/engine/dlssnr2fx.dll");
  auto eval = reinterpret_cast<EvalFn>(GetProcAddress(dll, "nr_eval"));
  auto cancel = reinterpret_cast<CancelFn>(GetProcAddress(dll, "nr_cancel"));
  auto shutdown = reinterpret_cast<ShutdownFn>(GetProcAddress(dll, "nr_shutdown"));
  if (!eval || !cancel || !shutdown) return Fail("resolve three C ABI exports");

  std::vector<uint8_t> input = {
      255, 0, 0, 255, 0, 255, 0, 255,
      0, 0, 255, 255, 128, 128, 128, 255};
  std::vector<uint8_t> output(input.size(), 0x5a);
  std::wstring missing = FullPath(L"test\\definitely-missing-runtime");
  nr_eval_desc d = MakeDesc(missing, input, output);
  auto start = std::chrono::steady_clock::now();
  nr_status status = eval(&d);
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start).count();
  if (status != NR_ERR_MISSING_RUNTIME) return Fail("missing runtime status");
  if (elapsed > 1000) return Fail("missing runtime bounded return");

  wchar_t temp[MAX_PATH]{};
  if (!GetTempPathW(MAX_PATH, temp)) return Fail("GetTempPath");
  const std::wstring partial = std::wstring(temp) + L"dlssnr2fx-engine-selftest";
  CreateDirectoryW(partial.c_str(), nullptr);
  const std::wstring snippet = partial + L"\\nvngx_dlssnr.dll";
  const std::wstring forwarder = partial + L"\\nvngx.dll_dlssnr.dll";
  DeleteFileW(snippet.c_str()); DeleteFileW(forwarder.c_str());
  if (!Touch(snippet)) return Fail("create snippet-only fixture");
  d = MakeDesc(partial, input, output);
  if (eval(&d) != NR_ERR_MISSING_RUNTIME) return Fail("missing forwarder status");
  DeleteFileW(snippet.c_str());
  if (!Touch(forwarder)) return Fail("create forwarder-only fixture");
  if (eval(&d) != NR_ERR_MISSING_RUNTIME) return Fail("missing snippet status");
  DeleteFileW(forwarder.c_str()); RemoveDirectoryW(partial.c_str());
  d = MakeDesc(missing, input, output);
  d.format = static_cast<nr_pixel_format>(99);
  if (eval(&d) != NR_ERR_ARGS) return Fail("invalid pixel format status");
  d.format = NR_PIX_RGBA8;

  d.instance_id = 200; d.request_id = 7;
  cancel(d.instance_id, d.request_id);
  if (eval(&d) != NR_ERR_SUPERSEDED) return Fail("pre-cancelled request status");
  d.instance_id = 201;
  if (eval(&d) != NR_ERR_MISSING_RUNTIME) return Fail("cancel isolation between instances");

  shutdown();
  shutdown();
  if (eval(&d) != NR_ERR_MISSING_RUNTIME) return Fail("eval after shutdown");

  nr_eval_desc p = d;
  p.width = 1920; p.height = 1080; p.model.intensity = 1.0f;
  SessionPolicy policy;
  if (!policy.NeedsRecreate(p)) return Fail("initial policy recreate");
  policy.Commit(p);
  if (policy.NeedsRecreate(p)) return Fail("same dimensions/model should reuse feature");
  p.detail = 0.1f; p.color = 0.2f; p.hdr = 1;
  if (policy.NeedsRecreate(p)) return Fail("composite controls should not recreate feature");
  p.model.intensity = 0.5f;
  if (!policy.NeedsRecreate(p)) return Fail("model change should recreate feature");

  SessionGate gate;
  auto held = gate.LockFor(std::chrono::milliseconds(100));
  if (!held.owns_lock()) return Fail("session gate initial lock");
  std::atomic<bool> busy{false};
  std::thread waiter([&]() {
    auto queued = gate.LockFor(std::chrono::milliseconds(25));
    busy = !queued.owns_lock();
  });
  waiter.join();
  if (!busy) return Fail("second session gate request should return busy");
  held.unlock();
  auto available = gate.LockFor(std::chrono::milliseconds(25));
  if (!available.owns_lock()) return Fail("session gate should recover after busy request");

  if (DecideFrameGuidance(false, 0, 0, 1, 10, 1, false, false) != FrameGuidanceAction::Reset)
    return Fail("frame guidance first eval resets");
  if (DecideFrameGuidance(true, 1, 10, 1, 10, 1, false, true) != FrameGuidanceAction::Coalesce)
    return Fail("frame guidance same key coalesces");
  if (DecideFrameGuidance(true, 1, 10, 1, 11, 1, false, false) != FrameGuidanceAction::Continue)
    return Fail("frame guidance next forward frame continues");
  if (DecideFrameGuidance(true, 1, 10, 1, 12, 1, false, false) != FrameGuidanceAction::Reset)
    return Fail("frame guidance skipped frame resets");
  if (DecideFrameGuidance(true, 1, 10, 1, 9, 1, false, false) != FrameGuidanceAction::Reset)
    return Fail("frame guidance reverse frame resets");
  if (DecideFrameGuidance(true, 1, 10, 2, 11, 1, false, false) != FrameGuidanceAction::Reset)
    return Fail("frame guidance instance change resets");
  if (DecideFrameGuidance(true, 1, 10, 1, 11, 0, false, false) != FrameGuidanceAction::Reset)
    return Fail("frame guidance missing step resets");
  if (DecideFrameGuidance(true, 1, 10, 1, 10, 1, true, true) != FrameGuidanceAction::Reset)
    return Fail("explicit reset must not be swallowed by coalesce");
  if (DecideFrameGuidance(true, 1, 10, 2, 10, 1, false, true) != FrameGuidanceAction::Reset)
    return Fail("instance change must not coalesce even if output key matches");
  if (DecideFrameGuidance(true, 1, 10, 1, 11, 1, false, false, FrameGuidanceMode::AlwaysReset) !=
      FrameGuidanceAction::Reset)
    return Fail("always-reset mode resets forward frames");
  if (DecideFrameGuidance(true, 1, 10, 1, 12, 1, false, false, FrameGuidanceMode::Lock) !=
      FrameGuidanceAction::Continue)
    return Fail("lock mode keeps history across skipped frames");
  if (DecideFrameGuidance(true, 1, 10, 1, 10, 1, false, true, FrameGuidanceMode::Auto, false) ==
      FrameGuidanceAction::Coalesce)
    return Fail("coalesce off must not coalesce");

  {
    constexpr int w = 32, h = 16;
    std::vector<uint8_t> prev(static_cast<size_t>(w) * h * 4, 0);
    std::vector<uint8_t> curr(prev.size(), 0);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const uint8_t v = static_cast<uint8_t>(x * 7 + y * 3);
        const size_t pi = (static_cast<size_t>(y) * w + x) * 4;
        prev[pi] = prev[pi + 1] = prev[pi + 2] = v;
        prev[pi + 3] = 255;
        const int sx = std::min(w - 1, x + 4);
        const size_t ci = (static_cast<size_t>(y) * w + sx) * 4;
        curr[ci] = curr[ci + 1] = curr[ci + 2] = v;
        curr[ci + 3] = 255;
      }
    }
    std::vector<float> xy;
    EstimateMvecRgba8(curr.data(), prev.data(), w, h, xy);
    if (xy.size() != static_cast<size_t>(w) * h * 2) return Fail("mvec size");
    double ax = 0;
    int n = 0;
    for (int y = 2; y < h - 2; ++y) {
      for (int x = 6; x < w - 6; ++x) {
        ax += xy[(static_cast<size_t>(y) * w + x) * 2];
        ++n;
      }
    }
    ax /= n;
    if (ax > -2.0 || ax < -6.0) return Fail("mvec current-to-previous sign/magnitude");
  }

  const uint8_t orig[] = {10, 20, 30, 40, 200, 180, 160, 140};
  const uint8_t nr[] = {110, 120, 130, 255, 20, 40, 60, 255};
  uint8_t composite[sizeof(orig)]{};
  CompositeRgba8(orig, nr, composite, 2, 0.0f, 1.0f, false);
  if (std::memcmp(orig, composite, sizeof(orig)) != 0) return Fail("detail=0 composite endpoint");
  CompositeRgba8(orig, nr, composite, 2, 1.0f, 1.0f, false);
  for (size_t i = 0; i < 2; ++i) {
    if (std::memcmp(composite + i * 4, nr + i * 4, 3) != 0)
      return Fail("detail=1 composite RGB endpoint");
    if (composite[i * 4 + 3] != orig[i * 4 + 3]) return Fail("composite alpha preservation");
  }

  const float origF[] = {1.0f / 32768.0f, 0.25f, 0.5f, 0.75f};
  const float nrF[] = {0.9f, 0.8f, 0.7f, 1.0f};
  float compositeF[4]{};
  CompositeRgba32f(origF, nrF, compositeF, 1, 0.0f, 1.0f, false);
  if (compositeF[0] != origF[0] || compositeF[3] != origF[3])
    return Fail("RGBA32F detail=0 preserves sub-8bit original");
  CompositeRgba32f(origF, nrF, compositeF, 1, 1.0f, 1.0f, false);
  if (compositeF[0] != nrF[0] || compositeF[3] != origF[3])
    return Fail("RGBA32F detail=1 RGB endpoint");

  {
    const float hdrIn[] = {0.f, 1.f, 182.625f, 0.5f, -0.25f, 0.5f, 0.75f, 0.9f};
    float proxy[8]{};
    PrepareModelRgba32f(hdrIn, proxy, 2);
    if (proxy[0] != 0.f || proxy[1] != 1.f || proxy[2] != 1.f || proxy[3] != 0.5f)
      return Fail("HDR proxy clamps 0/1/182.625 and keeps in-range alpha");
    if (proxy[4] != 0.f) return Fail("HDR proxy clamps finite negatives to 0");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float bad[] = {nan, inf, -inf, 2.f};
    float badProxy[4]{};
    PrepareModelRgba32f(bad, badProxy, 1);
    if (badProxy[0] != 0.f || badProxy[1] != 0.f || badProxy[2] != 0.f || badProxy[3] != 1.f)
      return Fail("HDR proxy sanitizes NaN/Inf and clamps alpha");

    float out[8]{};
    ReconstructRgba32f(hdrIn, proxy, proxy, out, 2, 0.0f, 1.0f);
    if (std::memcmp(hdrIn, out, sizeof(hdrIn)) != 0)
      return Fail("detail=0 reconstruct is exact original including HDR and negatives");
    ReconstructRgba32f(hdrIn, proxy, proxy, out, 2, 1.0f, 1.0f);
    if (out[2] != 182.625f || out[4] != -0.25f || out[3] != hdrIn[3] || out[7] != hdrIn[7])
      return Fail("neural==proxy restores original HDR range and alpha");
    if (out[2] <= 1.f) return Fail("reconstruct must not globally clamp HDR headroom");

    const float sdr[] = {0.1f, 0.2f, 0.3f, 0.4f};
    const float neu[] = {0.9f, 0.8f, 0.7f, 1.0f};
    float sdrProxy[4]{}, recon[4]{}, blended[4]{};
    PrepareModelRgba32f(sdr, sdrProxy, 1);
    ReconstructRgba32f(sdr, sdrProxy, neu, recon, 1, 0.5f, 1.0f);
    CompositeRgba32f(sdr, neu, blended, 1, 0.5f, 1.0f, false);
    for (int c = 0; c < 4; ++c)
      if (std::fabs(recon[c] - blended[c]) > 1.0e-6f)
        return Fail("SDR [0,1] reconstruct must match the old non-hdr composite");

    uint8_t a[8], b[8];
    std::memcpy(a, hdrIn, 8);
    std::memcpy(b, hdrIn, 8);
    if (NrHashBytes(a, 8) != NrHashBytes(b, 8))
      return Fail("content hash must not use buffer address");
    b[0] ^= 1;
    if (NrHashBytes(a, 8) == NrHashBytes(b, 8))
      return Fail("content hash must change when pixels change");

    std::vector<float> imgA(16, 0.25f), imgB(16, 0.75f), prev(16, 0.1f), mvec(8, 1.f), depth(4, 0.5f);
    std::vector<float> outA(16, 0.f);
    nr_eval_desc da{};
    da.width = 2; da.height = 2; da.format = NR_PIX_RGBA32F; da.instance_id = 1; da.frame_id = 10;
    da.detail = 1.f; da.color = 1.f; da.in_rgba = imgA.data(); da.out_rgba = outA.data();
    da.in_prev_rgba = prev.data(); da.in_mvec = mvec.data(); da.in_depth = depth.data();
    NrEvalIdentity ia = NrIdentityFromDesc(da);
    da.in_rgba = imgB.data();
    NrEvalIdentity ib = NrIdentityFromDesc(da);
    if (NrIdentityEqual(ia, ib)) return Fail("different inputs must not share eval identity");
    da.in_rgba = imgA.data();
    da.instance_id = 2;
    if (NrIdentityEqual(ia, NrIdentityFromDesc(da)))
      return Fail("different instances must not share eval identity");
    da.instance_id = 1;
    da.maps_only = 1;
    if (NrIdentityEqual(ia, NrIdentityFromDesc(da)))
      return Fail("maps-only must not match final-image identity");
    da.maps_only = 0;
    da.in_mvec = nullptr;
    if (NrIdentityEqual(ia, NrIdentityFromDesc(da)))
      return Fail("dropping external mvec must invalidate identity");
  }

  const bool runGpu = GetEnvironmentVariableW(L"DLSSNR2FX_RUN_GPU", nullptr, 0) > 0;
  if (runGpu) {
    std::wstring runtime = FullPath(L"runtime");
    constexpr int gpuWidth = 128, gpuHeight = 128;
    input.resize(static_cast<size_t>(gpuWidth) * gpuHeight * 4);
    for (int y = 0; y < gpuHeight; ++y) {
      for (int x = 0; x < gpuWidth; ++x) {
        const size_t i = (static_cast<size_t>(y) * gpuWidth + x) * 4;
        input[i + 0] = static_cast<uint8_t>(x * 255 / (gpuWidth - 1));
        input[i + 1] = static_cast<uint8_t>(y * 255 / (gpuHeight - 1));
        input[i + 2] = static_cast<uint8_t>((x ^ y) & 0xff);
        input[i + 3] = 255;
      }
    }
    output.assign(input.size(), 0);
    d = MakeDesc(runtime, input, output);
    d.width = gpuWidth; d.height = gpuHeight;
    d.timeout_ms = 120000;
    status = eval(&d);
    if (status != NR_OK) return Fail("optional GPU/NGX eval");
    bool nonzero = false;
    bool changed = false;
    for (size_t i = 0; i < output.size(); ++i) {
      nonzero |= output[i] != 0;
      changed |= output[i] != input[i];
    }
    if (!nonzero || output.size() != input.size()) return Fail("optional GPU round trip output");
    if (!changed) return Fail("optional GPU output identical to input");
    shutdown();
    output.assign(input.size(), 0);
    d.request_id = 2;
    status = eval(&d);
    if (status != NR_OK) return Fail("optional GPU eval after shutdown");
    shutdown();

    const size_t px = static_cast<size_t>(gpuWidth) * gpuHeight;
    std::vector<float> inF(px * 4), outHdr(px * 4, 0.f), outHdr2(px * 4, 0.f);
    for (size_t i = 0; i < px; ++i) {
      inF[i * 4 + 0] = (i % 4 == 0) ? 182.625f : 0.25f;
      inF[i * 4 + 1] = (i % 4 == 1) ? 1.0f : 0.25f;
      inF[i * 4 + 2] = (i % 4 == 2) ? 0.0f : 0.25f;
      inF[i * 4 + 3] = 0.8f;
    }
    inF[0] = -0.25f;
    nr_eval_desc hf{};
    hf.width = gpuWidth; hf.height = gpuHeight; hf.adapter = -1;
    hf.instance_id = 300; hf.request_id = 1; hf.timeout_ms = 120000;
    hf.model.preset = 0; hf.model.style = 0; hf.model.intensity = 1.0f;
    hf.model.local_structure = 1.0f; hf.model.local_tone = 1.0f;
    hf.model.skin = -1.0f; hf.model.global_tone = -1.0f;
    hf.detail = 0.0f; hf.color = 1.0f;
    hf.runtime_dir = runtime.c_str();
    hf.format = NR_PIX_RGBA32F;
    hf.frame_id = 7; hf.frame_step = 1; hf.fg_coalesce = 1;
    hf.in_rgba = inF.data(); hf.out_rgba = outHdr.data();
    status = eval(&hf);
    if (status != NR_OK) return Fail("optional GPU HDR detail=0 eval");
    if (std::memcmp(inF.data(), outHdr.data(), inF.size() * sizeof(float)) != 0)
      return Fail("GPU HDR detail=0 must be bit-exact original");
    hf.detail = 1.0f; hf.request_id = 2;
    std::fill(outHdr.begin(), outHdr.end(), 0.f);
    status = eval(&hf);
    if (status != NR_OK) return Fail("optional GPU HDR detail=1 eval");
    float maxRgb = 0.f;
    for (size_t i = 0; i < px; ++i) {
      maxRgb = std::max(maxRgb, outHdr[i * 4 + 0]);
      maxRgb = std::max(maxRgb, outHdr[i * 4 + 1]);
      maxRgb = std::max(maxRgb, outHdr[i * 4 + 2]);
      if (outHdr[i * 4 + 3] != inF[i * 4 + 3]) return Fail("GPU HDR alpha must pass through");
    }
    if (maxRgb <= 1.0f) return Fail("GPU HDR output must keep values above 1");
    std::vector<float> inF2 = inF;
    inF2[4] = 50.0f;
    hf.in_rgba = inF2.data(); hf.out_rgba = outHdr2.data(); hf.request_id = 3;
    status = eval(&hf);
    if (status != NR_OK) return Fail("optional GPU HDR second input eval");
    if (std::memcmp(outHdr.data(), outHdr2.data(), outHdr.size() * sizeof(float)) == 0)
      return Fail("same frame different input must not reuse cached output");
    hf.in_rgba = inF.data(); hf.out_rgba = outHdr2.data(); hf.request_id = 4;
    status = eval(&hf);
    if (status != NR_OK) return Fail("optional GPU HDR coalesce re-request");
    if (std::memcmp(outHdr.data(), outHdr2.data(), outHdr.size() * sizeof(float)) != 0)
      return Fail("identical HDR request should match previous result");
    shutdown();
    std::puts("GPU/NGX round trip + restart + HDR: PASS");
  } else {
    std::puts("GPU/NGX round trip: SKIP (set DLSSNR2FX_RUN_GPU=1 on the hard-redline machine)");
  }

  if (!runGpu) FreeLibrary(dll);
  std::printf("engine_selftest: PASS (missing runtime %lldms)\n", static_cast<long long>(elapsed));
  return 0;
}
