#include "ngx.h"

#include "gpu.h"

#include <cstdarg>
#include <cstdio>
#include <string>

namespace {
using PfnSetFloat = void(NVSDK_CONV*)(void*, const char*, float);
using PfnSetUInt = void(NVSDK_CONV*)(void*, const char*, unsigned int);

void LogNgx(const char* format, ...) {
  char line[1024]{};
  va_list args;
  va_start(args, format);
  vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
  va_end(args);
  OutputDebugStringA(line);
  OutputDebugStringA("\n");
  wchar_t temp[MAX_PATH]{};
  if (!GetTempPathW(MAX_PATH, temp)) return;
  FILE* file = nullptr;
  const std::wstring path = std::wstring(temp) + L"dlssnr2fx.log";
  if (_wfopen_s(&file, path.c_str(), L"ab") == 0 && file) {
    std::fprintf(file, "%s\r\n", line);
    std::fclose(file);
  }
}

bool FileExists(const std::wstring& path) {
  const DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool IsAbsolutePath(const std::wstring& path) {
  return path.size() >= 3 &&
         ((path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) ||
          (path[0] == L'\\' && path[1] == L'\\'));
}

HMODULE LoadDriverCore(bool& owned, std::wstring& loadedPath) {
  owned = false;
  HMODULE module = GetModuleHandleW(L"nvngx.dll");
  if (module) {
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(module, path, MAX_PATH)) loadedPath = path;
    return module;
  }

  wchar_t system[MAX_PATH]{};
  if (!GetSystemDirectoryW(system, MAX_PATH)) return nullptr;
  const std::wstring root = std::wstring(system) + L"\\DriverStore\\FileRepository";
  WIN32_FIND_DATAW data{};
  HANDLE find = FindFirstFileW((root + L"\\nv_dispsi.inf_amd64_*").c_str(), &data);
  if (find == INVALID_HANDLE_VALUE) return nullptr;
  do {
    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
    const std::wstring candidate = root + L"\\" + data.cFileName + L"\\nvngx.dll";
    if (!FileExists(candidate)) continue;
    module = LoadLibraryExW(candidate.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module) {
      owned = true;
      loadedPath = candidate;
      break;
    }
  } while (FindNextFileW(find, &data));
  FindClose(find);
  return module;
}

int DiscoverUIntSlot(NVSDK_NGX_Parameter* parameters) {
  if (!parameters) return -1;
  void** table = *reinterpret_cast<void***>(parameters);
  for (int slot = 0; slot < 8; ++slot) {
    constexpr unsigned probe = 0x1234u;
    unsigned readback = 0;
    reinterpret_cast<PfnSetUInt>(table[slot])(parameters, "DLSSNR2FX.UIntProbe", probe);
    if (parameters->Get("DLSSNR2FX.UIntProbe", &readback) == NVSDK_NGX_Result_Success &&
        readback == probe)
      return slot;
  }
  return -1;
}

int DiscoverFloatSlot(NVSDK_NGX_Parameter* parameters) {
  if (!parameters) return -1;
  void** table = *reinterpret_cast<void***>(parameters);
  for (int slot = 0; slot < 8; ++slot) {
    constexpr float probe = 0.3125f;
    float readback = 0.0f;
    reinterpret_cast<PfnSetFloat>(table[slot])(parameters, "DLSSNR2FX.FloatProbe", probe);
    if (parameters->Get("DLSSNR2FX.FloatProbe", &readback) == NVSDK_NGX_Result_Success &&
        readback == probe)
      return slot;
  }
  return -1;
}
}  // namespace

NgxRuntime::NgxRuntime() = default;

NgxRuntime::~NgxRuntime() {
  // The owning Session calls Shutdown with the device before destruction.
  if (module_) FreeLibrary(module_);
  if (ownsCoreModule_ && coreModule_) FreeLibrary(coreModule_);
}

nr_status NgxRuntime::Load(const std::wstring& runtimeDir, ID3D12Device* device) {
  if (!device || !IsAbsolutePath(runtimeDir)) return NR_ERR_ARGS;
  device_ = device;
  const std::wstring forwarderPath = runtimeDir + L"\\nvngx.dll_dlssnr.dll";
  snippetPath_ = runtimeDir + L"\\nvngx_dlssnr.dll";
  if (!FileExists(forwarderPath) || !FileExists(snippetPath_)) return NR_ERR_MISSING_RUNTIME;

  module_ = LoadLibraryExW(forwarderPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!module_) return NR_ERR_MISSING_RUNTIME;
#define RESOLVE(member, name) member = reinterpret_cast<decltype(member)>(GetProcAddress(module_, name))
  RESOLVE(setSlots_, "fwd_set_slots");
  RESOLVE(create_, "fwd_create");
  RESOLVE(evaluate_, "fwd_evaluate");
  RESOLVE(release_, "fwd_release");
#undef RESOLVE
  if (!setSlots_ || !create_ || !evaluate_ || !release_) {
    LogNgx("forwarder missing required fwd_* exports");
    return NR_ERR_NGX;
  }

  std::wstring corePath;
  coreModule_ = LoadDriverCore(ownsCoreModule_, corePath);
  if (!coreModule_) {
    LogNgx("driver NGX core not found");
    return NR_ERR_NGX;
  }
  const auto init = reinterpret_cast<PFN_NVSDK_NGX_D3D12_Init_ProjectID>(
      GetProcAddress(coreModule_, "NVSDK_NGX_D3D12_Init_ProjectID"));
  const auto getCapability = reinterpret_cast<PFN_NVSDK_NGX_D3D12_GetCapabilityParameters>(
      GetProcAddress(coreModule_, "NVSDK_NGX_D3D12_GetCapabilityParameters"));
  destroyParameters_ = reinterpret_cast<PFN_NVSDK_NGX_D3D12_DestroyParameters>(
      GetProcAddress(coreModule_, "NVSDK_NGX_D3D12_DestroyParameters"));
  shutdown_ = reinterpret_cast<PFN_NVSDK_NGX_D3D12_Shutdown1>(
      GetProcAddress(coreModule_, "NVSDK_NGX_D3D12_Shutdown1"));
  if (!init || !getCapability || !destroyParameters_ || !shutdown_) {
    LogNgx("driver NGX core missing required exports");
    return NR_ERR_NGX;
  }

  wchar_t temp[MAX_PATH]{};
  const DWORD n = GetTempPathW(MAX_PATH, temp);
  dataPath_ = (n && n < MAX_PATH) ? std::wstring(temp) + L"dlssnr2fx" : runtimeDir;
  CreateDirectoryW(dataPath_.c_str(), nullptr);
  NVSDK_NGX_Result result = init("b5a5e4a1-9a52-4d1c-9d3f-6d0b7f2c1e84", 0, "1.0",
                                  dataPath_.c_str(), device, NVSDK_NGX_Version_API, nullptr);
  if (result != NVSDK_NGX_Result_Success) {
    LogNgx("driver core Init_ProjectID failed: 0x%08X", static_cast<unsigned>(result));
    return NR_ERR_NGX;
  }
  result = getCapability(&parameters_);
  if (result != NVSDK_NGX_Result_Success || !parameters_) {
    LogNgx("driver core GetCapabilityParameters failed: 0x%08X", static_cast<unsigned>(result));
    return NR_ERR_NGX;
  }

  const int uintSlot = DiscoverUIntSlot(parameters_);
  const int floatSlot = DiscoverFloatSlot(parameters_);
  if (uintSlot < 0 || floatSlot < 0) {
    LogNgx("forwarder slot discovery failed: uint=%d float=%d", uintSlot, floatSlot);
    return NR_ERR_NGX;
  }
  setSlots_(uintSlot, floatSlot);
  char forwarderUtf8[MAX_PATH * 3]{};
  char coreUtf8[MAX_PATH * 3]{};
  WideCharToMultiByte(CP_UTF8, 0, forwarderPath.c_str(), -1, forwarderUtf8,
                      static_cast<int>(sizeof(forwarderUtf8)), nullptr, nullptr);
  WideCharToMultiByte(CP_UTF8, 0, corePath.c_str(), -1, coreUtf8,
                      static_cast<int>(sizeof(coreUtf8)), nullptr, nullptr);
  LogNgx("forwarder loaded: %s; driver core: %s; slots uint=%d float=%d", forwarderUtf8,
         coreUtf8, uintSlot, floatSlot);
  return NR_OK;
}

nr_status NgxRuntime::CreateFeature(ID3D12GraphicsCommandList* list, uint32_t width,
                                    uint32_t height, const nr_model& model) {
  if (!list || !parameters_ || !create_) return NR_ERR_NGX;
  ReleaseFeature();
  NrForwardModel forwarded{};
  forwarded.preset = model.preset;
  forwarded.intensity = model.intensity;
  forwarded.style = model.style;
  forwarded.localStructure = model.local_structure;
  forwarded.localTone = model.local_tone;
  forwarded.skinStructure = model.skin;
  forwarded.globalTone = model.global_tone;
  forwarded.autoMask = model.auto_mask;
  forwarded.uiCorrection = model.ui_correction;
  feature_ = create_(snippetPath_.c_str(), dataPath_.c_str(), device_, list, parameters_, width,
                     height, width, height, &forwarded);
  return feature_ ? NR_OK : NR_ERR_NGX;
}

nr_status NgxRuntime::Evaluate(ID3D12GraphicsCommandList* list, const GpuContext& gpu, bool reset) {
  if (!list || !feature_ || !parameters_ || !evaluate_) return NR_ERR_NGX;
  const int result = evaluate_(list, feature_, parameters_, gpu.Color(), gpu.Depth(), gpu.Motion(),
                               gpu.Output(), gpu.Width(), gpu.Height(), gpu.Width(), gpu.Height(),
                               reset ? 1 : 0);
  return static_cast<NVSDK_NGX_Result>(result) == NVSDK_NGX_Result_Success ? NR_OK : NR_ERR_NGX;
}

void NgxRuntime::ReleaseFeature() {
  if (feature_ && release_) release_(feature_);
  feature_ = nullptr;
}

void NgxRuntime::Shutdown(ID3D12Device* device) {
  ReleaseFeature();
  if (parameters_ && destroyParameters_) destroyParameters_(parameters_);
  parameters_ = nullptr;
  // The forwarder owns a separately initialized snippet instance but exposes no shutdown ABI.
  // Calling driver-core Shutdown1 while that instance is alive crashes the shipping runtime.
  // Retain the tiny driver loader reference process-wide; feature and capability resources above
  // are still released, and Windows reclaims the loader at process exit.
  (void)device;
  device_ = nullptr;
  if (module_) FreeLibrary(module_);
  if (ownsCoreModule_ && coreModule_)
    LogNgx("retaining driver NGX loader until process exit (forwarder has no shutdown export)");
  module_ = nullptr;
  coreModule_ = nullptr;
  ownsCoreModule_ = false;
  setSlots_ = nullptr;
  create_ = nullptr;
  evaluate_ = nullptr;
  release_ = nullptr;
  destroyParameters_ = nullptr;
  shutdown_ = nullptr;
  snippetPath_.clear();
  dataPath_.clear();
}
