#include "runtime.h"

#include <windows.h>

#include "log.h"
#include "srw.h"

namespace {
SRWLOCK gRuntimeMutex = SRWLOCK_INIT;
std::wstring gCached;

std::wstring ParentDir(std::wstring path) {
  size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return L".";
  return path.substr(0, slash);
}
bool FileExists(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}
bool HasNgxRuntime(const std::wstring& dir) {
  return FileExists(dir + L"\\nvngx.dll_dlssnr.dll") &&
         FileExists(dir + L"\\nvngx_dlssnr.dll");
}
void LogPath(const char* prefix, const std::wstring& path) {
  char utf[MAX_PATH * 3]{};
  WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf, sizeof(utf), nullptr, nullptr);
  Logf("%s %s", prefix, utf);
}
HMODULE ModuleFromHere() {
  HMODULE module = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&FindRuntimeDir), &module);
  return module;
}
}  // namespace

bool EnvTruthy(const wchar_t* name) {
  wchar_t buf[16]{};
  DWORD n = GetEnvironmentVariableW(name, buf, 16);
  if (!n || n >= 16) return false;
  return buf[0] == L'1' || buf[0] == L'y' || buf[0] == L'Y' ||
         buf[0] == L't' || buf[0] == L'T';
}

std::wstring FindRuntimeDir() {
  SrwExclusive guard(gRuntimeMutex);
  wchar_t env[MAX_PATH]{};
  DWORD n = GetEnvironmentVariableW(L"DLSSNR_RUNTIME_DIR", env, MAX_PATH);
  if (n > 0 && n < MAX_PATH && HasNgxRuntime(env)) {
    if (gCached != env) LogPath("runtime from DLSSNR_RUNTIME_DIR", env);
    gCached = env;
    return gCached;
  }
  if (!gCached.empty() && HasNgxRuntime(gCached)) return gCached;
  gCached.clear();

  wchar_t modulePath[MAX_PATH]{};
  HMODULE module = ModuleFromHere();
  if (module) GetModuleFileNameW(module, modulePath, MAX_PATH);
  std::wstring dir = ParentDir(modulePath);
  for (int i = 0; i < 8 && !dir.empty(); ++i) {
    const std::wstring child = dir + L"\\runtime";
    if (HasNgxRuntime(child)) {
      gCached = child;
      LogPath("runtime found", gCached);
      return gCached;
    }
    if (HasNgxRuntime(dir)) {
      gCached = dir;
      LogPath("runtime found next to plug-in", gCached);
      return gCached;
    }
    std::wstring parent = ParentDir(dir);
    if (parent == dir) break;
    dir = parent;
  }
  Logf("runtime NOT found (need nvngx.dll_dlssnr.dll + nvngx_dlssnr.dll; optional DLSSNR_RUNTIME_DIR)");
  return {};
}
