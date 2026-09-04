#pragma once

#include "dlssnr2fx.h"

#include <chrono>
#include <cstring>
#include <mutex>

class SessionGate {
public:
  using Lock = std::unique_lock<std::timed_mutex>;
  Lock LockUntil(std::chrono::steady_clock::time_point deadline) {
    Lock lock(mutex_, std::defer_lock);
    const bool acquired = lock.try_lock_until(deadline);
    (void)acquired;
    return lock;
  }
  Lock LockFor(std::chrono::milliseconds timeout) {
    Lock lock(mutex_, std::defer_lock);
    const bool acquired = lock.try_lock_for(timeout);
    (void)acquired;
    return lock;
  }
private:
  std::timed_mutex mutex_;
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
