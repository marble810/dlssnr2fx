#include "dlssnr2fx.h"

#include "composite.h"
#include "gpu.h"
#include "ngx.h"
#include "session_policy.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
enum class State { Idle, Starting, Ready, Busy, Stopping, Dead };

std::mutex gTableMutex;
std::unordered_map<int, std::shared_ptr<class Session>> gSessions;
std::mutex gCancelMutex;
std::unordered_map<uint32_t, uint32_t> gCancelledThrough;
std::mutex gLogMutex;

void Log(const char* format, ...) {
  char line[1024]{};
  va_list args;
  va_start(args, format);
  vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
  va_end(args);
  OutputDebugStringA(line);
  OutputDebugStringA("\n");
  std::lock_guard<std::mutex> guard(gLogMutex);
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
  std::lock_guard<std::mutex> guard(gCancelMutex);
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

    const bool recreate = policy_.NeedsRecreate(desc);
    if (recreate) {
      if (RemainingMs(deadline) == 0) return NR_ERR_TIMEOUT;
      ngx_.ReleaseFeature();
      if (!gpu_.CreateFrameResources(static_cast<uint32_t>(desc.width),
                                     static_cast<uint32_t>(desc.height)))
        return Fail(NR_ERR_GPU, "frame resource creation failed");
      if (!gpu_.Begin()) return Fail(NR_ERR_GPU, "command list reset failed");
      nr_status status = ngx_.CreateFeature(gpu_.List(), static_cast<uint32_t>(desc.width),
                                            static_cast<uint32_t>(desc.height), desc.model);
      const uint32_t remain = RemainingMs(deadline);
      bool submitted = gpu_.EndAndWait(remain ? remain : 1);
      if (status != NR_OK || !submitted)
        return Fail(status != NR_OK ? status : NR_ERR_TIMEOUT, "CreateFeature failed");
      policy_.Commit(desc);
      firstFrame_ = true;
      ++createCount_;
      Log("adapter %d CreateFeature count=%llu size=%dx%d intensity=%.3f style=%u",
          desc.adapter, static_cast<unsigned long long>(createCount_), desc.width, desc.height,
          desc.model.intensity, desc.model.style);
    }

    if (RemainingMs(deadline) == 0) return NR_ERR_TIMEOUT;
    state_ = State::Busy;
    const size_t bytes = static_cast<size_t>(desc.width) * desc.height * 4;
    uint32_t uploadTimeout = RemainingMs(deadline);
    if (!uploadTimeout) { state_ = State::Ready; return NR_ERR_TIMEOUT; }
    if (!gpu_.UploadInputs(desc.in_rgba, bytes, uploadTimeout))
      return Fail(RemainingMs(deadline) ? NR_ERR_GPU : NR_ERR_TIMEOUT, "input upload failed");
    if (RemainingMs(deadline) == 0) { state_ = State::Ready; return NR_ERR_TIMEOUT; }
    if (!gpu_.Begin()) return Fail(NR_ERR_GPU, "evaluate command list reset failed");
    nr_status status = ngx_.Evaluate(gpu_.List(), gpu_, firstFrame_);
    uint32_t remain = RemainingMs(deadline);
    bool submitted = gpu_.EndAndWait(remain ? remain : 1);
    if (status != NR_OK || !submitted)
      return Fail(status != NR_OK ? status : NR_ERR_TIMEOUT, "EvaluateFeature failed");
    firstFrame_ = false;

    std::vector<uint8_t> neural;
    remain = RemainingMs(deadline);
    if (!remain) { state_ = State::Ready; return NR_ERR_TIMEOUT; }
    if (!gpu_.ReadOutput(neural, remain)) return Fail(NR_ERR_TIMEOUT, "output readback failed");
    if (IsCancelled(desc.instance_id, desc.request_id)) {
      state_ = State::Ready;
      Log("adapter %d request %u:%u superseded after Evaluate", desc.adapter,
          desc.instance_id, desc.request_id);
      return NR_ERR_SUPERSEDED;
    }
    CompositeRgba8(desc.in_rgba, neural.data(), desc.out_rgba,
                   static_cast<size_t>(desc.width) * desc.height,
                   desc.detail, desc.color, desc.hdr != 0);
    state_ = State::Ready;
    Log("nr_eval OK adapter=%d request=%u:%u size=%dx%d", desc.adapter,
        desc.instance_id, desc.request_id, desc.width, desc.height);
    return NR_OK;
  }

  bool Shutdown() {
    auto lock = gate_.LockFor(std::chrono::milliseconds(2000));
    if (!lock.owns_lock()) return false;
    state_ = State::Stopping;
    ngx_.Shutdown(gpu_.Device());
    gpu_.Shutdown();
    policy_.Reset();
    runtimeDir_.clear();
    state_ = State::Idle;
    return true;
  }

private:
  nr_status Fail(nr_status status, const char* message) {
    Log("session failure status=%d: %s", static_cast<int>(status), message);
    ngx_.Shutdown(gpu_.Device());
    gpu_.Shutdown();
    policy_.Reset();
    state_ = State::Dead;
    retryAfter_ = Clock::now() + std::chrono::seconds(1);
    return status;
  }
  void ResetLocked() {
    ngx_.Shutdown(gpu_.Device());
    gpu_.Shutdown();
    policy_.Reset();
    runtimeDir_.clear();
    state_ = State::Idle;
  }

  SessionGate gate_;
  State state_ = State::Idle;
  Clock::time_point retryAfter_{};
  std::wstring runtimeDir_;
  GpuContext gpu_;
  NgxRuntime ngx_;
  SessionPolicy policy_;
  bool firstFrame_ = true;
  uint64_t createCount_ = 0;
};

bool Validate(const nr_eval_desc* desc) {
  if (!desc || !desc->in_rgba || !desc->out_rgba || !desc->runtime_dir ||
      desc->width <= 0 || desc->height <= 0 || desc->timeout_ms == 0)
    return false;
  const uint64_t pixels = static_cast<uint64_t>(desc->width) * static_cast<uint64_t>(desc->height);
  return pixels <= static_cast<uint64_t>(std::numeric_limits<size_t>::max() / 4) &&
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
      std::lock_guard<std::mutex> guard(gTableMutex);
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
    std::lock_guard<std::mutex> guard(gCancelMutex);
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
      std::lock_guard<std::mutex> guard(gTableMutex);
      for (auto& item : gSessions) sessions.push_back(item);
    }
    for (const auto& item : sessions) {
      if (item.second->Shutdown()) {
        std::lock_guard<std::mutex> guard(gTableMutex);
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
