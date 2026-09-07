#include "dlssnr2fx.h"

#include "color_adapt.h"
#include "composite.h"
#include "dav2.h"
#include "eval_identity.h"
#include "frame_guidance.h"
#include "gpu.h"
#include "mvec.h"
#include "ngx.h"
#include "nvof.h"
#include "session_policy.h"
#include "srw.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
enum class State { Idle, Starting, Ready, Busy, Stopping, Dead };

SRWLOCK gTableMutex = SRWLOCK_INIT;
std::unordered_map<int, std::shared_ptr<class Session>> gSessions;
SRWLOCK gCancelMutex = SRWLOCK_INIT;
std::unordered_map<uint32_t, uint32_t> gCancelledThrough;
SRWLOCK gLogMutex = SRWLOCK_INIT;

void Log(const char* format, ...) {
  char line[1024]{};
  va_list args;
  va_start(args, format);
  vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
  va_end(args);
  OutputDebugStringA(line);
  OutputDebugStringA("\n");
  SrwExclusive guard(gLogMutex);
  wchar_t temp[MAX_PATH]{};
  if (!GetTempPathW(MAX_PATH, temp)) return;
  std::wstring path = std::wstring(temp) + L"dlssnr2fx.log";
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"ab") == 0 && f) {
    fprintf(f, "%s\r\n", line);
    fclose(f);
  }
}

bool IsCancelled(uint32_t instance, uint32_t request) {
  SrwExclusive guard(gCancelMutex);
  auto it = gCancelledThrough.find(instance);
  return it != gCancelledThrough.end() && request <= it->second;
}

uint32_t RemainingMs(Clock::time_point deadline) {
  auto now = Clock::now();
  if (now >= deadline) return 0;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
  if (ms <= 0) return 1;
  return static_cast<uint32_t>(std::min<int64_t>(ms, std::numeric_limits<uint32_t>::max()));
}

bool RuntimeFileExists(const wchar_t* dir) {
  if (!dir || !*dir) return false;
  std::wstring path(dir);
  if (path.size() < 3 || (path[1] != L':' && !(path[0] == L'\\' && path[1] == L'\\')))
    return false;
  const std::wstring snippet = path + L"\\nvngx_dlssnr.dll";
  const std::wstring forwarder = path + L"\\nvngx.dll_dlssnr.dll";
  DWORD snippetAttr = GetFileAttributesW(snippet.c_str());
  DWORD forwarderAttr = GetFileAttributesW(forwarder.c_str());
  return snippetAttr != INVALID_FILE_ATTRIBUTES && !(snippetAttr & FILE_ATTRIBUTE_DIRECTORY) &&
         forwarderAttr != INVALID_FILE_ATTRIBUTES && !(forwarderAttr & FILE_ATTRIBUTE_DIRECTORY);
}

class Session {
public:
  nr_status Eval(const nr_eval_desc& desc, Clock::time_point deadline) {
    auto lock = gate_.LockUntil(deadline);
    if (!lock.owns_lock()) return NR_ERR_BUSY;
    if (IsCancelled(desc.instance_id, desc.request_id)) return NR_ERR_SUPERSEDED;

    if (state_ == State::Dead) {
      if (Clock::now() < retryAfter_) return NR_ERR_DEAD;
      ResetLocked();
    }
    if (runtimeDir_ != desc.runtime_dir && state_ != State::Idle) ResetLocked();

    if (state_ == State::Idle) {
      state_ = State::Starting;
      runtimeDir_ = desc.runtime_dir;
      if (RemainingMs(deadline) == 0) { state_ = State::Idle; return NR_ERR_TIMEOUT; }
      if (!gpu_.Initialize(desc.adapter)) return Fail(NR_ERR_GPU, "D3D12 initialization failed");
      nr_status status = ngx_.Load(runtimeDir_, gpu_.Device());
      if (status != NR_OK) return Fail(status, "NGX initialization failed");
      state_ = State::Ready;
      Log("adapter %d session ready", desc.adapter);
    }

    if (!desc.maps_only && (gpu_.Width() != static_cast<uint32_t>(desc.width) ||
                            gpu_.Height() != static_cast<uint32_t>(desc.height))) {
      DropAllInstances();
      if (RemainingMs(deadline) == 0) return NR_ERR_TIMEOUT;
      if (!gpu_.CreateFrameResources(static_cast<uint32_t>(desc.width),
                                     static_cast<uint32_t>(desc.height)))
        return Fail(NR_ERR_GPU, "frame resource creation failed");
    }
    InstanceState& inst = slot_;
    if (!desc.maps_only && (!inst.feature || inst.policy.NeedsRecreate(desc))) {
      if (RemainingMs(deadline) == 0) return NR_ERR_TIMEOUT;
      if (inst.feature) {
        ngx_.ReleaseFeature(inst.feature);
        inst.feature = nullptr;
      }
      if (!gpu_.Begin()) return Fail(NR_ERR_GPU, "command list reset failed");
      void* feature = nullptr;
      nr_status status = ngx_.CreateFeature(gpu_.List(), static_cast<uint32_t>(desc.width),
                                            static_cast<uint32_t>(desc.height), desc.model,
                                            &feature);
      const uint32_t remain = RemainingMs(deadline);
      bool submitted = gpu_.EndAndWait(remain ? remain : 1);
      if (status != NR_OK || !submitted || !feature)
        return Fail(status != NR_OK ? status : NR_ERR_TIMEOUT, "CreateFeature failed");
      inst.feature = feature;
      inst.policy.Commit(desc);
      inst.firstFrame = true;
      inst.lastEval = {};
      ++createCount_;
      Log("adapter %d CreateFeature count=%llu size=%dx%d intensity=%.3f style=%u",
          desc.adapter, static_cast<unsigned long long>(createCount_), desc.width, desc.height,
          desc.model.intensity, desc.model.style);
    }

    if (RemainingMs(deadline) == 0) return NR_ERR_TIMEOUT;
    const bool deep = desc.format == NR_PIX_RGBA32F;
    const size_t pixels = static_cast<size_t>(desc.width) * desc.height;
    const size_t bytes = pixels * (deep ? sizeof(float) * 4 : 4);
    FrameGuidanceMode fgMode = FrameGuidanceMode::Auto;
    if (desc.fg_mode == NR_FG_ALWAYS_RESET) fgMode = FrameGuidanceMode::AlwaysReset;
    else if (desc.fg_mode == NR_FG_LOCK) fgMode = FrameGuidanceMode::Lock;
    if (inst.lastEval.have && inst.lastEval.id.instance_id != desc.instance_id) {
      dav2_.Reset();
      nvof_.Reset();
    }
    const FrameGuidanceAction fg = DecideFrameGuidance(
        inst.lastEval.have, inst.lastEval.id.instance_id, inst.lastEval.id.frame_id,
        desc.instance_id, desc.frame_id, desc.frame_step, desc.reset != 0,
        LastMatches(inst.lastEval, desc, bytes), fgMode, desc.fg_coalesce != 0);
    auto emitGuide = [&](const float* mv, const float* z) {
      if (desc.out_mvec) {
        if (mv) std::memcpy(desc.out_mvec, mv, pixels * 2 * sizeof(float));
        else std::memset(desc.out_mvec, 0, pixels * 2 * sizeof(float));
      }
      if (desc.out_depth) {
        if (z) std::memcpy(desc.out_depth, z, pixels * sizeof(float));
        else std::memset(desc.out_depth, 0, pixels * sizeof(float));
      }
    };
    if (fg == FrameGuidanceAction::Coalesce && !desc.maps_only) {
      if (desc.out_rgba && !inst.lastEval.output.empty())
        std::memcpy(desc.out_rgba, inst.lastEval.output.data(), bytes);
      emitGuide(inst.lastEval.mvec.size() == pixels * 2 ? inst.lastEval.mvec.data() : nullptr,
                inst.lastEval.depth.size() == pixels ? inst.lastEval.depth.data() : nullptr);
      Log("nr_eval coalesce adapter=%d request=%u:%u frame=%lld",
          desc.adapter, desc.instance_id, desc.request_id,
          static_cast<long long>(desc.frame_id));
      return NR_OK;
    }
    // Natron evaluates out of order. Feature 18 history is last GPU Evaluate, not T-1.
    const bool hasPrev = desc.in_prev_rgba != nullptr;
    const bool ngxReset = fg == FrameGuidanceAction::Reset || inst.firstFrame;
    std::vector<float> proxy, prevProxy;
    const void* colorNow = desc.in_rgba;
    const void* colorPrev = desc.in_prev_rgba;
    if (deep) {
      proxy.resize(pixels * 4);
      PrepareModelRgba32f(static_cast<const float*>(desc.in_rgba), proxy.data(), pixels);
      colorNow = proxy.data();
      if (desc.in_prev_rgba) {
        prevProxy.resize(pixels * 4);
        PrepareModelRgba32f(static_cast<const float*>(desc.in_prev_rgba), prevProxy.data(), pixels);
        colorPrev = prevProxy.data();
      }
    }
    std::vector<float> mvec;
    std::vector<float> depth;
    const float* mvecPtr = nullptr;
    const float* depthPtr = nullptr;
    if (desc.in_mvec) {
      mvecPtr = desc.in_mvec;
    } else if (desc.mvec_source == NR_GUIDE_ESTIMATE && hasPrev && !ngxReset) {
      const bool nvofOk = deep
          ? nvof_.EstimateRgba32f(gpu_.AdapterLuid(), static_cast<const float*>(colorNow),
                                 static_cast<const float*>(colorPrev), desc.width,
                                 desc.height, desc.nvof_quality, desc.nvof_hint != 0, mvec)
          : nvof_.EstimateRgba8(gpu_.AdapterLuid(), static_cast<const uint8_t*>(colorNow),
                                static_cast<const uint8_t*>(colorPrev), desc.width,
                                desc.height, desc.nvof_quality, desc.nvof_hint != 0, mvec);
      if (!nvofOk) {
        if (deep)
          EstimateMvecRgba32f(static_cast<const float*>(colorNow),
                              static_cast<const float*>(colorPrev), desc.width, desc.height,
                              mvec);
        else
          EstimateMvecRgba8(static_cast<const uint8_t*>(colorNow),
                            static_cast<const uint8_t*>(colorPrev), desc.width, desc.height,
                            mvec);
      }
      if (mvec.size() == pixels * 2) mvecPtr = mvec.data();
    }
    if (desc.in_depth) {
      depthPtr = desc.in_depth;
    } else if (desc.depth_source == NR_GUIDE_ESTIMATE) {
      const int interval = desc.dav2_interval > 0 ? desc.dav2_interval : 4;
      const float ema = desc.dav2_ema >= 0.f ? desc.dav2_ema : 0.7f;
      const bool dav2Ok = deep
          ? dav2_.EstimateRgba32f(desc.runtime_dir, static_cast<const float*>(colorNow),
                                 desc.width, desc.height, interval, ema, desc.frame_id, depth)
          : dav2_.EstimateRgba8(desc.runtime_dir, static_cast<const uint8_t*>(colorNow),
                                desc.width, desc.height, interval, ema, desc.frame_id, depth);
      if (dav2Ok && depth.size() == pixels) depthPtr = depth.data();
    }
    const size_t mvecBytes = mvecPtr ? pixels * 2 * sizeof(float) : 0;
    const size_t depthBytes = depthPtr ? pixels * sizeof(float) : 0;
    emitGuide(mvecPtr, depthPtr);
    if (desc.maps_only) {
      inst.lastEval.mvec.assign(pixels * 2, 0.f);
      inst.lastEval.depth.assign(pixels, 0.f);
      if (mvecPtr) std::memcpy(inst.lastEval.mvec.data(), mvecPtr, pixels * 2 * sizeof(float));
      if (depthPtr) std::memcpy(inst.lastEval.depth.data(), depthPtr, pixels * sizeof(float));
      state_ = State::Ready;
      Log("nr_eval maps-only adapter=%d request=%u:%u mvec=%s depth=%s",
          desc.adapter, desc.instance_id, desc.request_id,
          desc.in_mvec ? "input" : (mvecPtr ? "est" : "zero"),
          desc.in_depth ? "input" : (depthPtr ? "est" : "zero"));
      return NR_OK;
    }
    state_ = State::Busy;
    uint32_t uploadTimeout = RemainingMs(deadline);
    if (!uploadTimeout) { state_ = State::Ready; return NR_ERR_TIMEOUT; }
    const bool uploaded = deep
        ? gpu_.UploadInputs(static_cast<const float*>(colorNow), bytes, mvecPtr, mvecBytes,
                            depthPtr, depthBytes, uploadTimeout)
        : gpu_.UploadInputs(static_cast<const uint8_t*>(colorNow), bytes, mvecPtr, mvecBytes,
                            depthPtr, depthBytes, uploadTimeout);
    if (!uploaded)
      return Fail(RemainingMs(deadline) ? NR_ERR_GPU : NR_ERR_TIMEOUT, "input upload failed");
    if (RemainingMs(deadline) == 0) { state_ = State::Ready; return NR_ERR_TIMEOUT; }
    if (!gpu_.Begin()) return Fail(NR_ERR_GPU, "evaluate command list reset failed");
    nr_status status = ngx_.Evaluate(gpu_.List(), gpu_, inst.feature, ngxReset);
    uint32_t remain = RemainingMs(deadline);
    bool submitted = gpu_.EndAndWait(remain ? remain : 1);
    if (status != NR_OK || !submitted)
      return Fail(status != NR_OK ? status : NR_ERR_TIMEOUT, "EvaluateFeature failed");

    remain = RemainingMs(deadline);
    if (!remain) {
      inst.firstFrame = true;
      state_ = State::Ready;
      return NR_ERR_TIMEOUT;
    }
    if (deep) {
      std::vector<float> neural;
      if (!gpu_.ReadOutput(neural, remain)) return Fail(NR_ERR_TIMEOUT, "output readback failed");
      if (IsCancelled(desc.instance_id, desc.request_id)) {
        inst.firstFrame = true;
        state_ = State::Ready;
        Log("adapter %d request %u:%u superseded after Evaluate", desc.adapter,
            desc.instance_id, desc.request_id);
        return NR_ERR_SUPERSEDED;
      }
      ReconstructRgba32f(static_cast<const float*>(desc.in_rgba), proxy.data(), neural.data(),
                         static_cast<float*>(desc.out_rgba), pixels, desc.detail, desc.color);
    } else {
      std::vector<uint8_t> neural;
      if (!gpu_.ReadOutput(neural, remain)) return Fail(NR_ERR_TIMEOUT, "output readback failed");
      if (IsCancelled(desc.instance_id, desc.request_id)) {
        inst.firstFrame = true;
        state_ = State::Ready;
        Log("adapter %d request %u:%u superseded after Evaluate", desc.adapter,
            desc.instance_id, desc.request_id);
        return NR_ERR_SUPERSEDED;
      }
      CompositeRgba8(static_cast<const uint8_t*>(desc.in_rgba), neural.data(),
                     static_cast<uint8_t*>(desc.out_rgba), pixels,
                     desc.detail, desc.color, desc.hdr != 0);
    }
    inst.firstFrame = false;
    RememberEval(inst.lastEval, desc, bytes);
    state_ = State::Ready;
    Log("nr_eval OK adapter=%d request=%u:%u size=%dx%d format=%s frame_guidance=%s frame=%lld prev=%d mvec=%s depth=%s",
        desc.adapter, desc.instance_id, desc.request_id, desc.width, desc.height,
        deep ? "RGBA32F" : "RGBA8", ngxReset ? "reset" : "continue",
        static_cast<long long>(desc.frame_id), hasPrev ? 1 : 0,
        desc.in_mvec ? "input" : (mvecPtr ? "est" : "zero"),
        desc.in_depth ? "input" : (depthPtr ? "est" : "zero"));
    return NR_OK;
  }

  bool Shutdown() {
    auto lock = gate_.LockFor(std::chrono::milliseconds(2000));
    if (!lock.owns_lock()) return false;
    state_ = State::Stopping;
    DropAllInstances();
    nvof_.Reset();
    dav2_.Reset();
    ngx_.Shutdown(gpu_.Device());
    gpu_.Shutdown();
    runtimeDir_.clear();
    state_ = State::Idle;
    return true;
  }

private:
  struct LastEval {
    bool have = false;
    NrEvalIdentity id{};
    std::vector<uint8_t> output;
    std::vector<float> mvec;
    std::vector<float> depth;
  };
  struct InstanceState {
    void* feature = nullptr;
    bool firstFrame = true;
    SessionPolicy policy;
    LastEval lastEval;
  };

  nr_status Fail(nr_status status, const char* message) {
    Log("session failure status=%d: %s", static_cast<int>(status), message);
    DropAllInstances();
    nvof_.Reset();
    dav2_.Reset();
    ngx_.Shutdown(gpu_.Device());
    gpu_.Shutdown();
    state_ = State::Dead;
    retryAfter_ = Clock::now() + std::chrono::seconds(1);
    return status;
  }
  void ResetLocked() {
    DropAllInstances();
    nvof_.Reset();
    dav2_.Reset();
    ngx_.Shutdown(gpu_.Device());
    gpu_.Shutdown();
    runtimeDir_.clear();
    state_ = State::Idle;
  }
  bool LastMatches(const LastEval& last, const nr_eval_desc& desc, size_t bytes) const {
    return last.have && last.output.size() == bytes &&
           NrIdentityEqual(last.id, NrIdentityFromDesc(desc));
  }
  void RememberEval(LastEval& last, const nr_eval_desc& desc, size_t bytes) {
    last.have = true;
    last.id = NrIdentityFromDesc(desc);
    last.output.resize(bytes);
    if (desc.out_rgba) std::memcpy(last.output.data(), desc.out_rgba, bytes);
    const size_t px = static_cast<size_t>(desc.width) * desc.height;
    last.mvec.assign(px * 2, 0.f);
    last.depth.assign(px, 0.f);
    if (desc.out_mvec) std::memcpy(last.mvec.data(), desc.out_mvec, px * 2 * sizeof(float));
    if (desc.out_depth) std::memcpy(last.depth.data(), desc.out_depth, px * sizeof(float));
  }
  void DropAllInstances() {
    if (slot_.feature) ngx_.ReleaseFeature(slot_.feature);
    slot_ = {};
  }

  SessionGate gate_;
  State state_ = State::Idle;
  Clock::time_point retryAfter_{};
  std::wstring runtimeDir_;
  GpuContext gpu_;
  NgxRuntime ngx_;
  NvofEstimator nvof_;
  Dav2Estimator dav2_;
  InstanceState slot_;
  uint64_t createCount_ = 0;
};

bool Validate(const nr_eval_desc* desc) {
  if (!desc || !desc->in_rgba || !desc->out_rgba || !desc->runtime_dir ||
      desc->width <= 0 || desc->height <= 0 || desc->timeout_ms == 0)
    return false;
  if (desc->format != NR_PIX_RGBA8 && desc->format != NR_PIX_RGBA32F)
    return false;
  const uint64_t pixels = static_cast<uint64_t>(desc->width) * static_cast<uint64_t>(desc->height);
  const uint64_t bpp = desc->format == NR_PIX_RGBA32F ? 16u : 4u;
  return pixels <= static_cast<uint64_t>(std::numeric_limits<size_t>::max() / bpp) &&
         pixels <= 0x3fffffffu;
}
}  // namespace

extern "C" NR_API nr_status NR_CALL nr_eval(const nr_eval_desc* desc) {
  try {
    if (!Validate(desc)) return NR_ERR_ARGS;
    const auto deadline = Clock::now() + std::chrono::milliseconds(desc->timeout_ms);
    if (IsCancelled(desc->instance_id, desc->request_id)) return NR_ERR_SUPERSEDED;
    if (!RuntimeFileExists(desc->runtime_dir)) return NR_ERR_MISSING_RUNTIME;
    // The default adapter is the first high-performance adapter, the same device as explicit index 0.
    const int key = desc->adapter < 0 ? 0 : desc->adapter;
    std::shared_ptr<Session> session;
    {
      SrwExclusive guard(gTableMutex);
      auto& slot = gSessions[key];
      if (!slot) slot = std::make_shared<Session>();
      session = slot;
    }
    return session->Eval(*desc, deadline);
  } catch (...) {
    Log("nr_eval swallowed an exception");
    return NR_ERR_DEAD;
  }
}

extern "C" NR_API void NR_CALL nr_cancel(uint32_t instanceId, uint32_t requestId) {
  try {
    SrwExclusive guard(gCancelMutex);
    auto& watermark = gCancelledThrough[instanceId];
    if (requestId > watermark) watermark = requestId;
    if (gCancelledThrough.size() > 4096) gCancelledThrough.erase(gCancelledThrough.begin());
  } catch (...) {
  }
}

extern "C" NR_API void NR_CALL nr_shutdown(void) {
  try {
    std::vector<std::pair<int, std::shared_ptr<Session>>> sessions;
    {
      SrwExclusive guard(gTableMutex);
      for (auto& item : gSessions) sessions.push_back(item);
    }
    for (const auto& item : sessions) {
      if (item.second->Shutdown()) {
        SrwExclusive guard(gTableMutex);
        auto it = gSessions.find(item.first);
        if (it != gSessions.end() && it->second == item.second) gSessions.erase(it);
      } else {
        Log("nr_shutdown left busy adapter %d alive after bounded wait", item.first);
      }
    }
  } catch (...) {
    Log("nr_shutdown swallowed an exception");
  }
}
