// Clean-room in-process shim named nvngx.dll_dlssnr.dll.
//
// The DLSS Neural Rendering snippet (nvngx_dlssnr.dll) verifies the module owning its
// caller's return address (RtlPcToFileHeader) and refuses callers whose module path does
// not contain "nvngx.dll".  This DLL exists so that every call into the snippet originates
// from a module the snippet accepts.  It contains no NVIDIA code: it only forwards the four
// engine calls (fwd_set_slots / fwd_create / fwd_evaluate / fwd_release) to the snippet's
// exported entry points, driving its parameter block through the raw vtable slot set.
//
// The ABI this module implements is defined by the host engine in engine/ngx_decl.h
// (PFN_FwdSetSlots / PFN_FwdCreate / PFN_FwdEvaluate / PFN_FwdRelease and ngx_key::*).
// The parameter block is the driver core's capability block; its setters are invoked through
// raw vtable slots because the block does not match the SDK header layout.  The uint/float
// slots are probed by the engine and handed in via fwd_set_slots; the resource carrier slot
// is verified here at first use.  All keys and calling shapes come from ngx_decl.h.

#include <windows.h>

#include "ngx_decl.h"

#include <cstdarg>
#include <cstdio>
#include <string>

namespace {

// --- Raw vtable slot call shapes (x64: __thiscall == __cdecl at the ABI level) ----------
using PfnSlotSetULL = void(NVSDK_CONV*)(void*, const char*, unsigned long long);
using PfnSlotSetUInt = void(NVSDK_CONV*)(void*, const char*, unsigned int);
using PfnSlotSetFloat = void(NVSDK_CONV*)(void*, const char*, float);
using PfnSlotGetResource = NVSDK_NGX_Result(NVSDK_CONV*)(const void*, const char*,
                                                         ID3D12Resource**);

// --- Snippet entry points (resolved dynamically; no import library) ----------------------
using PfnSnippetInitExt = NVSDK_NGX_Result(NVSDK_CONV*)(
    unsigned long long, const wchar_t*, ID3D12Device*, NVSDK_NGX_Version, const void*);
using PfnSnippetCreate = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*, int,
                                                       const void*, void**);
using PfnSnippetEvaluate = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*,
                                                         const void*, const void*, void*);
using PfnSnippetRelease = NVSDK_NGX_Result(NVSDK_CONV*)(void*);

// --- Singleton snippet instance (engine serialises per adapter; multi-GPU out of scope) ---
struct SnippetState {
  HMODULE module = nullptr;
  PfnSnippetInitExt init = nullptr;
  PfnSnippetCreate create = nullptr;
  PfnSnippetEvaluate evaluate = nullptr;
  PfnSnippetRelease release = nullptr;
  bool initialized = false;
  ID3D12Device* device = nullptr;
};
SnippetState g_snippet;
CRITICAL_SECTION g_lock;
bool g_lockReady = false;

int g_uintSlot = -1;
int g_floatSlot = -1;
int g_resourceSlot = -1;

void Log(const char* format, ...) {
  char line[1024]{};
  va_list args;
  va_start(args, format);
  vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
  va_end(args);
  OutputDebugStringA("dlssnr2fx-fwd: ");
  OutputDebugStringA(line);
  OutputDebugStringA("\n");
  wchar_t temp[MAX_PATH]{};
  if (!GetTempPathW(MAX_PATH, temp)) return;
  FILE* file = nullptr;
  const std::wstring path = std::wstring(temp) + L"dlssnr2fx.log";
  if (_wfopen_s(&file, path.c_str(), L"ab") == 0 && file) {
    std::fprintf(file, "dlssnr2fx-fwd: %s\r\n", line);
    std::fclose(file);
  }
}

class ScopedLock {
public:
  ScopedLock() { if (g_lockReady) EnterCriticalSection(&g_lock); }
  ~ScopedLock() { if (g_lockReady) LeaveCriticalSection(&g_lock); }
};

void** VTable(void* object) { return *reinterpret_cast<void***>(object); }

void SetUInt(void* block, const char* name, unsigned int value) {
  if (!block || g_uintSlot < 0 || g_uintSlot > 7) return;
  reinterpret_cast<PfnSlotSetUInt>(VTable(block)[g_uintSlot])(block, name, value);
}

void SetFloat(void* block, const char* name, float value) {
  if (!block || g_floatSlot < 0 || g_floatSlot > 7) return;
  reinterpret_cast<PfnSlotSetFloat>(VTable(block)[g_floatSlot])(block, name, value);
}

void SetResource(void* block, const char* name, ID3D12Resource* value) {
  if (!block || g_resourceSlot < 0 || g_resourceSlot > 7) return;
  reinterpret_cast<PfnSlotSetULL>(VTable(block)[g_resourceSlot])(block, name,
                                                                reinterpret_cast<unsigned long long>(value));
}

ID3D12Resource* CreateProbeResource(ID3D12Device* device) {
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = 64;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ID3D12Resource* resource = nullptr;
  if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_COMMON, nullptr,
                                             IID_PPV_ARGS(&resource))))
    return nullptr;
  return resource;
}

// Verify which raw slot round-trips an ID3D12Resource* through the block.  The setter
// carriers are pointer-sized (ULL slot 0 is the observed one), but the block does not
// follow the SDK header layout, so the readback Get slot is located by scanning the
// pointer-valued Get positions (8..15) too.  If nothing round-trips, fall back to the
// observed resource carrier slot 0 with a warning; the real Evaluate round trip is the
// final arbiter.
int DiscoverResourceSlot(void* block, ID3D12Device* device) {
  if (!block || !device) return -1;
  ID3D12Resource* probe = CreateProbeResource(device);
  if (!probe) return -1;
  const char* const probeName = "DLSSNR2FX.ResourceProbe";
  int found = -1;
  for (int set = 0; set < 8 && found < 0; ++set) {
    for (int get = 8; get < 16 && found < 0; ++get) {
      ID3D12Resource* readback = nullptr;
      reinterpret_cast<PfnSlotSetULL>(VTable(block)[set])(
          block, probeName, reinterpret_cast<unsigned long long>(probe));
      if (reinterpret_cast<PfnSlotGetResource>(VTable(block)[get])(block, probeName,
                                                                  &readback) ==
              NVSDK_NGX_Result_Success &&
          readback == probe) {
        found = set;
        Log("resource slot=%d via Get slot %d", set, get);
      }
    }
  }
  if (found < 0) {
    found = 0;
    Log("resource slot verification failed; falling back to carrier slot 0");
  }
  probe->Release();
  return found;
}

bool LoadSnippet(const wchar_t* snippetPath) {
  if (g_snippet.module) return true;
  g_snippet.module = LoadLibraryExW(snippetPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!g_snippet.module) {
    Log("snippet load failed: %ls", snippetPath);
    return false;
  }
#define RESOLVE(member, name) \
  g_snippet.member = reinterpret_cast<decltype(g_snippet.member)>(GetProcAddress(g_snippet.module, name))
  RESOLVE(init, "NVSDK_NGX_D3D12_Init_Ext");
  RESOLVE(create, "NVSDK_NGX_D3D12_CreateFeature");
  RESOLVE(evaluate, "NVSDK_NGX_D3D12_EvaluateFeature");
  RESOLVE(release, "NVSDK_NGX_D3D12_ReleaseFeature");
#undef RESOLVE
  if (!g_snippet.init || !g_snippet.create || !g_snippet.evaluate || !g_snippet.release) {
    Log("snippet missing required entry points");
    FreeLibrary(g_snippet.module);
    g_snippet.module = nullptr;
    return false;
  }
  Log("snippet loaded: %ls", snippetPath);
  return true;
}

void SetCreateParams(void* block, unsigned width, unsigned height, unsigned outWidth,
                     unsigned outHeight, const NrForwardModel* model) {
  SetUInt(block, ngx_key::CreationNodeMask, 0);
  SetUInt(block, ngx_key::VisibilityNodeMask, 0);
  SetUInt(block, ngx_key::Width, width);
  SetUInt(block, ngx_key::Height, height);
  SetUInt(block, ngx_key::OutWidth, outWidth);
  SetUInt(block, ngx_key::OutHeight, outHeight);
  SetUInt(block, ngx_key::Enabled, 1);
  SetUInt(block, ngx_key::Upscaling, 0);
  SetUInt(block, ngx_key::InputWidth, width);
  SetUInt(block, ngx_key::InputHeight, height);
  SetUInt(block, ngx_key::OutputWidth, outWidth);
  SetUInt(block, ngx_key::OutputHeight, outHeight);
  SetUInt(block, ngx_key::NrWidth, width);
  SetUInt(block, ngx_key::NrHeight, height);
  if (model) {
    SetUInt(block, ngx_key::Preset, model->preset);
    SetFloat(block, ngx_key::Intensity, model->intensity);
    SetUInt(block, ngx_key::Style, model->style);
    SetFloat(block, ngx_key::LocalStructure, model->localStructure);
    SetFloat(block, ngx_key::LocalTone, model->localTone);
    SetFloat(block, ngx_key::Skin, model->skinStructure);
    SetFloat(block, ngx_key::GlobalTone, model->globalTone);
    SetUInt(block, ngx_key::UseAutoMask, model->autoMask);
    SetUInt(block, ngx_key::UICorrection, model->uiCorrection);
  }
}

}  // namespace

extern "C" {

__declspec(dllexport) void fwd_set_slots(int uintSlot, int floatSlot) {
  ScopedLock lock;
  g_uintSlot = (uintSlot >= 0 && uintSlot < 8) ? uintSlot : -1;
  g_floatSlot = (floatSlot >= 0 && floatSlot < 8) ? floatSlot : -1;
  g_resourceSlot = -1;
  Log("slots uint=%d float=%d", g_uintSlot, g_floatSlot);
}

// Create a Neural Rendering feature: initialise the snippet, verify the resource slot,
// fill the engine's create-time keys into the driver capability block, then call the
// snippet's CreateFeature.  Returns nullptr on any failure (host maps to NR_ERR_NGX).
__declspec(dllexport) void* fwd_create(const wchar_t* snippetPath, const wchar_t* dataPath,
                                       ID3D12Device* device,
                                       ID3D12GraphicsCommandList* list, void* block,
                                       unsigned width, unsigned height, unsigned outWidth,
                                       unsigned outHeight, const NrForwardModel* model) {
  ScopedLock lock;
  if (!snippetPath || !dataPath || !device || !list || !block) {
    Log("create: bad args");
    return nullptr;
  }
  if (!LoadSnippet(snippetPath)) return nullptr;

  if (g_resourceSlot < 0) {
    g_resourceSlot = DiscoverResourceSlot(block, device);
    if (g_resourceSlot < 0) {
      Log("resource slot discovery failed");
      return nullptr;
    }
    Log("resource slot=%d", g_resourceSlot);
  }

  if (!g_snippet.initialized) {
    const NVSDK_NGX_Result initResult =
        g_snippet.init(0ull, dataPath, device, NVSDK_NGX_Version_API, nullptr);
    if (NgxFailed(initResult)) {
      Log("snippet Init_Ext failed: 0x%08X", static_cast<unsigned>(initResult));
      return nullptr;
    }
    g_snippet.initialized = true;
    g_snippet.device = device;
    Log("snippet initialized");
  } else if (g_snippet.device != device) {
    Log("create: snippet already initialised for another adapter (multi-GPU unsupported)");
    return nullptr;
  }

  SetCreateParams(block, width, height, outWidth, outHeight, model);

  void* handle = nullptr;
  const NVSDK_NGX_Result createResult =
      g_snippet.create(list, kNgxFeatureNeuralRendering, block, &handle);
  if (NgxFailed(createResult) || !handle) {
    Log("snippet CreateFeature failed: 0x%08X", static_cast<unsigned>(createResult));
    return nullptr;
  }
  Log("feature created %ux%u(%ux%u)", width, height, outWidth, outHeight);
  return handle;
}

__declspec(dllexport) int fwd_evaluate(ID3D12GraphicsCommandList* list, void* feature,
                                       void* block, ID3D12Resource* color,
                                       ID3D12Resource* depth, ID3D12Resource* motion,
                                       ID3D12Resource* output, unsigned width,
                                       unsigned height, unsigned outWidth, unsigned outHeight,
                                       int reset) {
  ScopedLock lock;
  if (!list || !feature || !block || !color || !depth || !motion || !output) return 0;
  SetResource(block, ngx_key::Color, color);
  SetResource(block, ngx_key::Depth, depth);
  SetResource(block, ngx_key::MVec, motion);
  SetResource(block, ngx_key::Output, output);
  SetUInt(block, ngx_key::Width, width);
  SetUInt(block, ngx_key::Height, height);
  SetUInt(block, ngx_key::OutWidth, outWidth);
  SetUInt(block, ngx_key::OutHeight, outHeight);
  SetUInt(block, ngx_key::InputWidth, width);
  SetUInt(block, ngx_key::InputHeight, height);
  SetUInt(block, ngx_key::OutputWidth, outWidth);
  SetUInt(block, ngx_key::OutputHeight, outHeight);
  SetUInt(block, ngx_key::NrWidth, width);
  SetUInt(block, ngx_key::NrHeight, height);
  SetUInt(block, ngx_key::ColorSubrectWidth, width);
  SetUInt(block, ngx_key::ColorSubrectHeight, height);
  SetUInt(block, ngx_key::DepthSubrectWidth, width);
  SetUInt(block, ngx_key::DepthSubrectHeight, height);
  SetUInt(block, ngx_key::MVecSubrectWidth, width);
  SetUInt(block, ngx_key::MVecSubrectHeight, height);
  SetUInt(block, ngx_key::OutputSubrectWidth, outWidth);
  SetUInt(block, ngx_key::OutputSubrectHeight, outHeight);
  SetUInt(block, ngx_key::DepthInverted, 0);
  SetFloat(block, ngx_key::MVecScaleX, 1.0f);
  SetFloat(block, ngx_key::MVecScaleY, 1.0f);
  SetUInt(block, ngx_key::Reset, reset ? 1u : 0u);
  const NVSDK_NGX_Result result = g_snippet.evaluate(list, feature, block, nullptr);
  if (NgxFailed(result)) {
    Log("snippet EvaluateFeature failed: 0x%08X reset=%d", static_cast<unsigned>(result), reset);
  }
  return static_cast<int>(result);
}

__declspec(dllexport) void fwd_release(void* feature) {
  ScopedLock lock;
  if (feature && g_snippet.release) {
    const NVSDK_NGX_Result result = g_snippet.release(feature);
    if (NgxFailed(result))
      Log("snippet ReleaseFeature failed: 0x%08X", static_cast<unsigned>(result));
  }
}

}  // extern "C"

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    InitializeCriticalSection(&g_lock);
    g_lockReady = true;
  } else if (reason == DLL_PROCESS_DETACH) {
    g_lockReady = false;
    DeleteCriticalSection(&g_lock);
    if (g_snippet.module) {
      // No snippet shutdown export is part of the forwarder ABI.  Per architectural
      // decision the driver snippet stays loaded until process exit, so we do not
      // FreeLibrary here; Windows reclaims the module at process teardown.
      g_snippet.module = nullptr;
    }
  }
  return TRUE;
}