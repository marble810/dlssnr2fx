#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace {

std::mutex gLogMu;
char gLogPath[MAX_PATH] = {0};

void EnsurePath() {
  if (gLogPath[0])
    return;
  char tmp[MAX_PATH];
  DWORD n = GetTempPathA(MAX_PATH, tmp);
  if (n == 0 || n >= MAX_PATH) {
    snprintf(gLogPath, MAX_PATH, "dlssnr-ofx.log");
    return;
  }
  snprintf(gLogPath, MAX_PATH, "%sdlssnr-ofx.log", tmp);
}

}  // namespace

void LogInit() {
  EnsurePath();
}

void Logf(const char* fmt, ...) {
  EnsurePath();
  char msg[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  SYSTEMTIME st;
  GetLocalTime(&st);
  char line[2304];
  snprintf(line, sizeof(line), "%04d-%02d-%02d %02d:%02d:%02d.%03d [%lu] %s\n", st.wYear, st.wMonth,
           st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentThreadId(), msg);

  OutputDebugStringA(line);
  std::lock_guard<std::mutex> lock(gLogMu);
  FILE* f = nullptr;
  if (fopen_s(&f, gLogPath, "a") == 0 && f) {
    fputs(line, f);
    fclose(f);
  }
}
