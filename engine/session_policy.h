#pragma once

#include "dlssnr2fx.h"

#include <windows.h>

#include <chrono>
#include <cstring>

class SessionLock {
public:
  SessionLock() = default;
  SessionLock(HANDLE mutex, DWORD timeoutMs) {
    if (!mutex) return;
    if (WaitForSingleObject(mutex, timeoutMs) == WAIT_OBJECT_0) {
      mutex_ = mutex;
      owned_ = true;
    }
  }
  ~SessionLock() {
    if (owned_ && mutex_) ReleaseMutex(mutex_);
  }
  SessionLock(SessionLock&& other) noexcept {
    mutex_ = other.mutex_;
    owned_ = other.owned_;
    other.mutex_ = nullptr;
    other.owned_ = false;
  }
  SessionLock& operator=(SessionLock&& other) noexcept {
    if (this == &other) return *this;
    if (owned_ && mutex_) ReleaseMutex(mutex_);
    mutex_ = other.mutex_;
    owned_ = other.owned_;
    other.mutex_ = nullptr;
    other.owned_ = false;
    return *this;
  }
  SessionLock(const SessionLock&) = delete;
  SessionLock& operator=(const SessionLock&) = delete;
  bool owns_lock() const { return owned_; }
  void unlock() {
    if (owned_ && mutex_) ReleaseMutex(mutex_);
    owned_ = false;
  }

private:
  HANDLE mutex_ = nullptr;
  bool owned_ = false;
};

class SessionGate {
public:
  using Lock = SessionLock;
  SessionGate() { mutex_ = CreateMutexW(nullptr, FALSE, nullptr); }
  ~SessionGate() {
    if (mutex_) CloseHandle(mutex_);
  }
  SessionGate(const SessionGate&) = delete;
  SessionGate& operator=(const SessionGate&) = delete;
  Lock LockUntil(std::chrono::steady_clock::time_point deadline) {
    const auto now = std::chrono::steady_clock::now();
    DWORD ms = 0;
    if (deadline > now) {
      const auto count =
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
      ms = count <= 0 ? 0 : (count > 0xffffffffLL ? 0xffffffffu : static_cast<DWORD>(count));
    }
    return Lock(mutex_, ms);
  }
  Lock LockFor(std::chrono::milliseconds timeout) {
    const auto count = timeout.count();
    const DWORD ms = count <= 0 ? 0 : (count > 0xffffffffLL ? 0xffffffffu : static_cast<DWORD>(count));
    return Lock(mutex_, ms);
  }

private:
  HANDLE mutex_ = nullptr;
};

inline bool NrModelEqual(const nr_model& a, const nr_model& b) {
  return a.preset == b.preset && a.style == b.style && a.auto_mask == b.auto_mask &&
         a.ui_correction == b.ui_correction && a.intensity == b.intensity &&
         a.local_structure == b.local_structure && a.local_tone == b.local_tone &&
         a.skin == b.skin && a.global_tone == b.global_tone;
}

struct SessionPolicy {
  bool configured = false;
  int width = 0;
  int height = 0;
  nr_model model{};

  bool NeedsRecreate(const nr_eval_desc& desc) const {
    return !configured || width != desc.width || height != desc.height || !NrModelEqual(model, desc.model);
  }
  void Commit(const nr_eval_desc& desc) {
    configured = true;
    width = desc.width;
    height = desc.height;
    model = desc.model;
  }
  void Reset() { configured = false; }
};
