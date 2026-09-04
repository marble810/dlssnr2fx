#pragma once

// Minimal ABI declarations copied from the public NGX headers. This project
// deliberately neither includes an installed NVIDIA SDK nor links an NGX import library.

#include <d3d12.h>
#include <d3d11.h>
#include <stdint.h>

#define NVSDK_CONV __cdecl

typedef uint32_t NVSDK_NGX_Result;
typedef int32_t NVSDK_NGX_Feature;
typedef uint32_t NVSDK_NGX_Version;
struct NVSDK_NGX_Handle;

static constexpr NVSDK_NGX_Result NVSDK_NGX_Result_Success = 0x00000001u;
static constexpr NVSDK_NGX_Result NVSDK_NGX_Result_Fail = 0xBAD00000u;
static constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_InvalidParameter = 0xBAD00005u;
static constexpr NVSDK_NGX_Version NVSDK_NGX_Version_API = 0x00000015u;
static constexpr NVSDK_NGX_Feature kNgxFeatureNeuralRendering = 18;

inline bool NgxFailed(NVSDK_NGX_Result result) {
  return (result & 0xFFF00000u) == NVSDK_NGX_Result_Fail;
}

// The virtual method order is ABI-significant and mirrors nvsdk_ngx_params.h.
struct NVSDK_NGX_Parameter {
  virtual void Set(const char* name, unsigned long long value) = 0;
  virtual void Set(const char* name, float value) = 0;
  virtual void Set(const char* name, double value) = 0;
  virtual void Set(const char* name, unsigned int value) = 0;
  virtual void Set(const char* name, int value) = 0;
  virtual void Set(const char* name, ID3D11Resource* value) = 0;
  virtual void Set(const char* name, ID3D12Resource* value) = 0;
  virtual void Set(const char* name, void* value) = 0;

  virtual NVSDK_NGX_Result Get(const char* name, unsigned long long* value) const = 0;
  virtual NVSDK_NGX_Result Get(const char* name, float* value) const = 0;
  virtual NVSDK_NGX_Result Get(const char* name, double* value) const = 0;
  virtual NVSDK_NGX_Result Get(const char* name, unsigned int* value) const = 0;
  virtual NVSDK_NGX_Result Get(const char* name, int* value) const = 0;
  virtual NVSDK_NGX_Result Get(const char* name, ID3D11Resource** value) const = 0;
  virtual NVSDK_NGX_Result Get(const char* name, ID3D12Resource** value) const = 0;
  virtual NVSDK_NGX_Result Get(const char* name, void** value) const = 0;

  virtual void Reset() = 0;
protected:
  ~NVSDK_NGX_Parameter() = default;
};

using PFN_NVSDK_NGX_Parameter_SetUI = void(NVSDK_CONV*)(NVSDK_NGX_Parameter*, const char*, unsigned int);
using PFN_NVSDK_NGX_Parameter_SetF = void(NVSDK_CONV*)(NVSDK_NGX_Parameter*, const char*, float);
using PFN_NVSDK_NGX_Parameter_SetD3d12Resource = void(NVSDK_CONV*)(NVSDK_NGX_Parameter*, const char*, ID3D12Resource*);
using PFN_NVSDK_NGX_Parameter_GetUI = NVSDK_NGX_Result(NVSDK_CONV*)(const NVSDK_NGX_Parameter*, const char*, unsigned int*);
using PFN_NVSDK_NGX_Parameter_GetF = NVSDK_NGX_Result(NVSDK_CONV*)(const NVSDK_NGX_Parameter*, const char*, float*);
using PFN_NVSDK_NGX_Parameter_GetD3d12Resource = NVSDK_NGX_Result(NVSDK_CONV*)(const NVSDK_NGX_Parameter*, const char*, ID3D12Resource**);

// Driver-core entry points are resolved dynamically; there is no NGX import library.
using PFN_NVSDK_NGX_D3D12_Init_Ext = NVSDK_NGX_Result(NVSDK_CONV*)(
    unsigned long long, const wchar_t*, ID3D12Device*, NVSDK_NGX_Version, const void*);
using PFN_NVSDK_NGX_D3D12_Init_ProjectID = NVSDK_NGX_Result(NVSDK_CONV*)(
    const char*, int, const char*, const wchar_t*, ID3D12Device*, NVSDK_NGX_Version,
    const void*);
using PFN_NVSDK_NGX_D3D12_GetCapabilityParameters =
    NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter**);
using PFN_NVSDK_NGX_D3D12_DestroyParameters =
    NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter*);
using PFN_NVSDK_NGX_D3D12_Shutdown1 = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);

// Exact POD/calling contract exported by the shipping nvngx.dll_dlssnr.dll.
struct NrForwardModel {
  unsigned preset;
  float intensity;
  unsigned style;
  float localStructure;
  float localTone;
  float skinStructure;
  float globalTone;
  unsigned autoMask;
  unsigned uiCorrection;
};
using PFN_FwdSetSlots = void(NVSDK_CONV*)(int, int);
using PFN_FwdCreate = void*(NVSDK_CONV*)(
    const wchar_t*, const wchar_t*, ID3D12Device*, ID3D12GraphicsCommandList*, void*,
    unsigned, unsigned, unsigned, unsigned, const NrForwardModel*);
using PFN_FwdEvaluate = int(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, void*, void*, ID3D12Resource*, ID3D12Resource*,
    ID3D12Resource*, ID3D12Resource*, unsigned, unsigned, unsigned, unsigned, int);
using PFN_FwdRelease = void(NVSDK_CONV*)(void*);

namespace ngx_key {
inline constexpr const char* CreationNodeMask = "CreationNodeMask";
inline constexpr const char* VisibilityNodeMask = "VisibilityNodeMask";
inline constexpr const char* Width = "Width";
inline constexpr const char* Height = "Height";
inline constexpr const char* OutWidth = "OutWidth";
inline constexpr const char* OutHeight = "OutHeight";
inline constexpr const char* Enabled = "DLSSNR.Enabled";
inline constexpr const char* Upscaling = "DLSSNR.Upscaling";
inline constexpr const char* InputWidth = "DLSSNR.InputWidth";
inline constexpr const char* InputHeight = "DLSSNR.InputHeight";
inline constexpr const char* OutputWidth = "DLSSNR.OutputWidth";
inline constexpr const char* OutputHeight = "DLSSNR.OutputHeight";
inline constexpr const char* NrWidth = "DLSSNR.Width";
inline constexpr const char* NrHeight = "DLSSNR.Height";
inline constexpr const char* Preset = "DLSSNR.Hint.Render.Preset";
inline constexpr const char* Intensity = "DLSSNR.Intensity";
inline constexpr const char* Style = "DLSSNR.Style";
inline constexpr const char* LocalStructure = "DLSSNR.LocalStructureStrength";
inline constexpr const char* LocalTone = "DLSSNR.LocalToneStrength";
inline constexpr const char* Skin = "DLSSNR.SkinStructureStrength";
inline constexpr const char* GlobalTone = "DLSSNR.GlobalToneStrength";
inline constexpr const char* UseAutoMask = "DLSSNR.UseAutoMask";
inline constexpr const char* UICorrection = "DLSSNR.UICorrection";
inline constexpr const char* DepthInverted = "DLSSNR.DepthInverted";
inline constexpr const char* Color = "DLSSNR.Color";
inline constexpr const char* Depth = "DLSSNR.Depth";
inline constexpr const char* MVec = "DLSSNR.MVec";
inline constexpr const char* Output = "DLSSNR.Output";
inline constexpr const char* Reset = "DLSSNR.Reset";
inline constexpr const char* MVecScaleX = "DLSSNR.MVecScaleX";
inline constexpr const char* MVecScaleY = "DLSSNR.MVecScaleY";
inline constexpr const char* ColorSubrectWidth = "DLSSNR.ColorSubrectWidth";
inline constexpr const char* ColorSubrectHeight = "DLSSNR.ColorSubrectHeight";
inline constexpr const char* DepthSubrectWidth = "DLSSNR.DepthSubrectWidth";
inline constexpr const char* DepthSubrectHeight = "DLSSNR.DepthSubrectHeight";
inline constexpr const char* MVecSubrectWidth = "DLSSNR.MVecSubrectWidth";
inline constexpr const char* MVecSubrectHeight = "DLSSNR.MVecSubrectHeight";
inline constexpr const char* OutputSubrectWidth = "DLSSNR.OutputSubrectWidth";
inline constexpr const char* OutputSubrectHeight = "DLSSNR.OutputSubrectHeight";
}  // namespace ngx_key
