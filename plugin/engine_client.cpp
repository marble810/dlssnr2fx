#include "engine_client.h"

#include "log.h"

#include <windows.h>

#include <mutex>
#include <string>

namespace engine_client {
namespace {
using EvalFn = nr_status(NR_CALL*)(const nr_eval_desc*);
using CancelFn = void(NR_CALL*)(uint32_t, uint32_t);
using ShutdownFn = void(NR_CALL*)();

std::mutex gMutex;
HMODULE gModule = nullptr;
EvalFn gEval = nullptr;
CancelFn gCancel = nullptr;
ShutdownFn gShutdown = nullptr;
bool gAttempted = false;

std::wstring ParentDir(std::wstring path) {
  size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

bool LoadLocked() {
  if (gModule) return true;
  if (gAttempted) return false;
  gAttempted = true;
  HMODULE self = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&LoadLocked), &self);
  wchar_t modulePath[MAX_PATH]{};
  if (!self || !GetModuleFileNameW(self, modulePath, MAX_PATH)) {
    Logf("engine client: cannot resolve plug-in module path");
    return false;
  }
  std::wstring path = ParentDir(modulePath) + L"\\dlssnr2fx.dll";
  gModule = LoadLibraryW(path.c_str());
  if (!gModule) {
    Logf("engine client: dlssnr2fx.dll missing beside plug-in (winerr=%lu)", GetLastError());
    return false;
  }
  gEval = reinterpret_cast<EvalFn>(GetProcAddress(gModule, "nr_eval"));
  gCancel = reinterpret_cast<CancelFn>(GetProcAddress(gModule, "nr_cancel"));
  gShutdown = reinterpret_cast<ShutdownFn>(GetProcAddress(gModule, "nr_shutdown"));
  if (!gEval || !gCancel || !gShutdown) {
    Logf("engine client: invalid dlssnr2fx.dll exports");
    FreeLibrary(gModule);
    gModule = nullptr;
    gEval = nullptr; gCancel = nullptr; gShutdown = nullptr;
    return false;
  }
  Logf("engine client: loaded dlssnr2fx.dll beside plug-in");
  return true;
}
}  // namespace

nr_status Eval(const nr_eval_desc& desc) {
  EvalFn fn = nullptr;
  {
    std::lock_guard<std::mutex> guard(gMutex);
    if (!LoadLocked()) return NR_ERR_MISSING_RUNTIME;
    fn = gEval;
  }
  return fn ? fn(&desc) : NR_ERR_MISSING_RUNTIME;
}

void Cancel(uint32_t instanceId, uint32_t requestId) {
  CancelFn fn = nullptr;
  {
    std::lock_guard<std::mutex> guard(gMutex);
    if (!gModule) return;
    fn = gCancel;
  }
  if (fn) fn(instanceId, requestId);
}

void Shutdown() {
  std::lock_guard<std::mutex> guard(gMutex);
  if (gShutdown) gShutdown();
  // Keep the engine module resident for the host process. The shipping NR forwarder has no
  // shutdown export and leaves snippet-owned code/data referenced after feature release;
  // FreeLibrary(dlssnr2fx.dll) at that point crashes on driver 616.56. Sessions and GPU resources
  // are released by nr_shutdown, and Windows reclaims the resident modules at process exit.
}

const char* StatusName(nr_status status) {
  switch (status) {
    case NR_OK: return "OK";
    case NR_ERR_MISSING_RUNTIME: return "MISSING_RUNTIME";
    case NR_ERR_GPU: return "GPU";
    case NR_ERR_NGX: return "NGX";
    case NR_ERR_TIMEOUT: return "TIMEOUT";
    case NR_ERR_BUSY: return "BUSY";
    case NR_ERR_SUPERSEDED: return "SUPERSEDED";
    case NR_ERR_ARGS: return "ARGS";
    case NR_ERR_DEAD: return "DEAD";
  }
  return "UNKNOWN";
}
}  // namespace engine_client
