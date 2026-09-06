#pragma once

#include "dlssnr2fx.h"
#include "ngx_decl.h"

#include <windows.h>

#include <string>
#include <unordered_set>

class GpuContext;

class NgxRuntime {
public:
  NgxRuntime();
  ~NgxRuntime();
  nr_status Load(const std::wstring& runtimeDir, ID3D12Device* device);
  nr_status CreateFeature(ID3D12GraphicsCommandList* list, uint32_t width, uint32_t height,
                          const nr_model& model, void** outFeature);
  nr_status Evaluate(ID3D12GraphicsCommandList* list, const GpuContext& gpu, void* feature,
                     bool reset);
  void ReleaseFeature(void* feature);
  void ReleaseAllFeatures();
  void Shutdown(ID3D12Device* device);
  bool Loaded() const { return module_ != nullptr; }

private:
  HMODULE module_ = nullptr;
  HMODULE coreModule_ = nullptr;
  bool ownsCoreModule_ = false;
  PFN_FwdSetSlots setSlots_ = nullptr;
  PFN_FwdCreate create_ = nullptr;
  PFN_FwdEvaluate evaluate_ = nullptr;
  PFN_FwdRelease release_ = nullptr;
  PFN_NVSDK_NGX_D3D12_DestroyParameters destroyParameters_ = nullptr;
  PFN_NVSDK_NGX_D3D12_Shutdown1 shutdown_ = nullptr;
  NVSDK_NGX_Parameter* parameters_ = nullptr;
  ID3D12Device* device_ = nullptr;
  std::unordered_set<void*> features_;
  std::wstring snippetPath_;
  std::wstring dataPath_;
};
