#pragma once

#include <windows.h>

// Zero-init SRWLOCK is valid. Global std::mutex under /std:c++17 can be
// constant-initialized to null and crash in MSVCP140!mtx_do_lock.
struct SrwExclusive {
  SRWLOCK* lock;
  explicit SrwExclusive(SRWLOCK& l) : lock(&l) { AcquireSRWLockExclusive(lock); }
  ~SrwExclusive() { ReleaseSRWLockExclusive(lock); }
  SrwExclusive(const SrwExclusive&) = delete;
  SrwExclusive& operator=(const SrwExclusive&) = delete;
};
