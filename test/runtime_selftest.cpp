#include "../plugin/runtime.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace {
bool Touch(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  CloseHandle(file);
  return true;
}
}

int main() {
  wchar_t temp[MAX_PATH]{};
  if (!GetTempPathW(MAX_PATH, temp)) return 1;
  std::wstring dir = std::wstring(temp) + L"dlssnr2fx-runtime-selftest";
  CreateDirectoryW(dir.c_str(), nullptr);
  const std::wstring exe = dir + L"\\dummy-helper.exe";
  const std::wstring snippet = dir + L"\\nvngx_dlssnr.dll";
  const std::wstring forwarder = dir + L"\\nvngx.dll_dlssnr.dll";
  DeleteFileW(snippet.c_str());
  DeleteFileW(forwarder.c_str());
  if (!Touch(exe)) return 2;
  SetEnvironmentVariableW(L"DLSSNR_RUNTIME_DIR", dir.c_str());
  if (FindRuntimeDir() == dir) return 3; // an exe alone must not satisfy discovery
  if (!Touch(snippet)) return 4;
  if (FindRuntimeDir() == dir) return 5; // snippet alone is a known-bad direct-init layout
  if (!Touch(forwarder)) return 6;
  if (FindRuntimeDir() != dir) return 7;
  DeleteFileW(exe.c_str());
  DeleteFileW(snippet.c_str());
  DeleteFileW(forwarder.c_str());
  RemoveDirectoryW(dir.c_str());
  SetEnvironmentVariableW(L"DLSSNR_RUNTIME_DIR", nullptr);
  std::puts("runtime_selftest: PASS");
  return 0;
}
