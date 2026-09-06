#pragma once

#include <windows.h>

// Zero-init SRWLOCK is valid. Global std::mutex under /std:c++17 can be
// constant-initialized to null and crash in MSVCP140!mtx_do_lock (Nuke load).
struct SrwExclusive {
  SRWLOCK* lock;
  explicit SrwExclusive(SRWLOCK& l) : lock(&l) { AcquireSRWLockExclusive(lock); }
  ~SrwExclusive() { ReleaseSRWLockExclusive(lock); }
  SrwExclusive(const SrwExclusive&) = delete;
  SrwExclusive& operator=(const SrwExclusive&) = delete;
};

struct CsLock {
  CRITICAL_SECTION* cs;
  explicit CsLock(CRITICAL_SECTION& c) : cs(&c) { EnterCriticalSection(cs); }
  ~CsLock() { LeaveCriticalSection(cs); }
  CsLock(const CsLock&) = delete;
  CsLock& operator=(const CsLock&) = delete;
};
