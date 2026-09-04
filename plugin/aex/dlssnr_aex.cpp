// DLSS Neural Rendering - After Effects 2025 (Win x64) effect plug-in.
// Classic PF (CPU) path: full-frame RENDER through the in-process dlssnr2fx engine.

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <windows.h>

#include "AEConfig.h"
#include "entry.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "AEFX_SuiteHelper.h"

#include "../engine_client.h"
#include "../log.h"
#include "../pixio.h"
#include "../runtime.h"
#include "dlssnr_aex_version.h"

enum {
  AEX_INPUT = 0,
  AEX_INTENSITY,
  AEX_STYLE,
  AEX_DETAIL,
  AEX_COLOR,
  AEX_UI_CORRECTION,
  AEX_LOCAL_STRUCTURE,
  AEX_LOCAL_TONE,
  AEX_SKIN,
  AEX_GLOBAL_TONE,
  AEX_MOTION,
  AEX_MOTION_ENGINE,
  AEX_MOTION_VIS,
  AEX_PRESET,
  AEX_HDR,
  AEX_AUTO_MASK,
  AEX_ADAPTER,
  AEX_HL_PROTECT,
  AEX_HL_THRESHOLD,
  AEX_HL_SOFTNESS,
  AEX_HL_MIX,
  AEX_HL_SHOW_MASK,
  AEX_NUM_PARAMS
};

static_assert(PF_VERSION(AEX_MAJOR_VERSION, AEX_MINOR_VERSION, AEX_BUG_VERSION, AEX_STAGE_VERSION,
                         AEX_BUILD_VERSION) == AEX_PIPL_VERSION,
              "AE code and PiPL versions must match");

namespace {

std::atomic<uint32_t> gRequestId{0};

bool Aborted(PF_InData* in_data) { return (bool)PF_ABORT(in_data); }
uint32_t InstanceId(PF_InData* in_data) {
  uintptr_t value = reinterpret_cast<uintptr_t>(in_data ? in_data->effect_ref : nullptr);
  return static_cast<uint32_t>((value >> 4) ^ (value >> 32) ^ 0xA3E50001u);
}

int DepthBytesFor(PF_PixelFormat fmt) {
  switch (fmt) {
    case PF_PixelFormat_ARGB64:
      return 2;
    case PF_PixelFormat_ARGB128:
      return 4;
    default:
      return 1;
  }
}

pix::PixWorld MakeAexWorld(PF_EffectWorld* world, PF_PixelFormat fmt) {
  pix::PixWorld pw;
  pw.data = reinterpret_cast<uint8_t*>(world->data);
  pw.rowBytes = world->rowbytes;
  pw.bounds = {0, 0, static_cast<int>(world->width), static_cast<int>(world->height)};
  pw.width = static_cast<int>(world->width);
  pw.height = static_cast<int>(world->height);
  pw.nComp = 4;
  pw.bytesPerComp = DepthBytesFor(fmt);
  // AE PF_Pixel memory is A,R,G,B; pixio exposes canonical R,G,B,A.
  pw.channelIndex[0] = 1;
  pw.channelIndex[1] = 2;
  pw.channelIndex[2] = 3;
  pw.channelIndex[3] = 0;
  if (pw.bytesPerComp == 2)
    pw.maxChannelValue = PF_MAX_CHAN16;
  return pw;
}

PF_Err GlobalSetup(PF_InData* /*in_data*/, PF_OutData* out_data) {
  LogInit();
  Logf("aex load");
  out_data->my_version = PF_VERSION(AEX_MAJOR_VERSION, AEX_MINOR_VERSION, AEX_BUG_VERSION, AEX_STAGE_VERSION,
                                    AEX_BUILD_VERSION);
  // Full-frame packed-RGBA contract; the engine serializes Evaluate per adapter.
  out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE;
  // FLOAT_COLOR_AWARE requires Smart Render, which this full-frame classic PF
  // implementation intentionally does not support.
  out_data->out_flags2 = 0;
  strncpy_s(out_data->name, "DLSS Neural Rendering", _TRUNCATE);
  return PF_Err_NONE;
}

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data) {
  PF_Err err = PF_Err_NONE;
  PF_ParamDef def;

  // Look group (order matches the OFX host version)
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Intensity", 0, 2, 0, 2, 1.0, 2, PF_ValueDisplayFlag_NONE, 0, AEX_INTENSITY);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("Style", 3, 1, "Default|Natural|Cinematic", AEX_STYLE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Detail", 0, 1, 0, 1, 1.0, 2, PF_ValueDisplayFlag_NONE, 0, AEX_DETAIL);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Color", 0, 1, 0, 1, 1.0, 2, PF_ValueDisplayFlag_NONE, 0, AEX_COLOR);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("UI Correction", FALSE, 0, AEX_UI_CORRECTION);

  // Structure group
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Local Structure", 0, 2, 0, 2, 1.0, 2, PF_ValueDisplayFlag_NONE, 0, AEX_LOCAL_STRUCTURE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Local Tone", 0, 2, 0, 2, 1.0, 2, PF_ValueDisplayFlag_NONE, 0, AEX_LOCAL_TONE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Skin", -1, 2, -1, 2, -1.0, 2, PF_ValueDisplayFlag_NONE, 0, AEX_SKIN);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Global Tone", -1, 2, -1, 2, -1.0, 2, PF_ValueDisplayFlag_NONE, 0, AEX_GLOBAL_TONE);

  // Motion group
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("Motion Vectors", FALSE, 0, AEX_MOTION);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("Motion Engine", 3, 1, "Auto|NVOF|Lucas-Kanade", AEX_MOTION_ENGINE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("Motion Visualize", FALSE, 0, AEX_MOTION_VIS);

  // Advanced group
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("NR Preset", 0, 8, 0, 8, 0, AEX_PRESET);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("HDR / Linear", FALSE, 0, AEX_HDR);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("Auto Mask", FALSE, 0, AEX_AUTO_MASK);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("GPU Adapter", -1, 8, -1, 8, -1, AEX_ADAPTER);

  // Highlight protection (same post-process and defaults as the OFX plug-in)
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("Highlight Protect", TRUE, 0, AEX_HL_PROTECT);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Highlight Threshold", 0, 1, 0, 1, 0.80, 2, PF_ValueDisplayFlag_NONE, 0,
                       AEX_HL_THRESHOLD);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Highlight Softness", 0, 1, 0, 1, 0.15, 2, PF_ValueDisplayFlag_NONE, 0,
                       AEX_HL_SOFTNESS);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Highlight Mix", 0, 1, 0, 1, 1.0, 2, PF_ValueDisplayFlag_NONE, 0,
                       AEX_HL_MIX);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("Highlight Show Mask", FALSE, 0, AEX_HL_SHOW_MASK);

  if (!err)
    out_data->num_params = AEX_NUM_PARAMS;
  return err;
}

PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
  PF_Err err = PF_Err_NONE;
  PF_EffectWorld* input = &params[AEX_INPUT]->u.ld;
  if (!input->data || !output || !output->data) {
    Logf("aex render: missing input/output world");
    return PF_Err_BAD_CALLBACK_PARAM;
  }

  A_long w = input->width;
  A_long h = input->height;
  PF_PixelFormat inputFormat = PF_PixelFormat_ARGB32;
  PF_PixelFormat outputFormat = PF_PixelFormat_ARGB32;
  {
    AEFX_SuiteScoper<PF_WorldSuite2> ws(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
    if (ws->PF_GetPixelFormat(input, &inputFormat) != PF_Err_NONE)
      inputFormat = PF_PixelFormat_ARGB32;
    if (ws->PF_GetPixelFormat(output, &outputFormat) != PF_Err_NONE)
      outputFormat = inputFormat;
  }

  pix::PixWorld src = MakeAexWorld(input, inputFormat);
  pix::PixWorld dst = MakeAexWorld(output, outputFormat);
  auto passthrough = [&]() {
    pix::CopyPassthrough(src, dst);
    return PF_Err_NONE;
  };

  if (w <= 0 || h <= 0 || output->width != w || output->height != h) {
    Logf("aex render: incompatible worlds input=%dx%d output=%dx%d", (int)w, (int)h,
         (int)output->width, (int)output->height);
    return passthrough();
  }

  if (EnvTruthy(L"DLSSNR_PASSTHROUGH")) {
    Logf("aex render: passthrough (env) %dx%d", (int)w, (int)h);
    return passthrough();
  }

  if (Aborted(in_data)) {
    Logf("aex render: abort before NR %dx%d", (int)w, (int)h);
    return passthrough();
  }
  std::wstring runtime = FindRuntimeDir();
  if (runtime.empty()) {
    Logf("aex render: runtime NOT found; passthrough");
    return passthrough();
  }

  std::vector<uint8_t> packed, outPacked;
  // PF_EffectWorld rows are top-to-bottom; no vertical conversion is needed.
  pix::PackRgba8(src, packed, /*flipVert=*/false);
  outPacked.resize(packed.size());
  nr_eval_desc desc{};
  desc.width = static_cast<int32_t>(w);
  desc.height = static_cast<int32_t>(h);
  desc.adapter = params[AEX_ADAPTER]->u.sd.value;
  desc.instance_id = InstanceId(in_data);
  desc.request_id = gRequestId.fetch_add(1) + 1;
  desc.timeout_ms = 15000;
  desc.model.intensity = static_cast<float>(params[AEX_INTENSITY]->u.fs_d.value);
  desc.model.style = params[AEX_STYLE]->u.pd.value > 0 ? params[AEX_STYLE]->u.pd.value - 1 : 0;
  desc.detail = static_cast<float>(params[AEX_DETAIL]->u.fs_d.value);
  desc.color = static_cast<float>(params[AEX_COLOR]->u.fs_d.value);
  desc.model.ui_correction = params[AEX_UI_CORRECTION]->u.bd.value != FALSE;
  desc.model.local_structure = static_cast<float>(params[AEX_LOCAL_STRUCTURE]->u.fs_d.value);
  desc.model.local_tone = static_cast<float>(params[AEX_LOCAL_TONE]->u.fs_d.value);
  desc.model.skin = static_cast<float>(params[AEX_SKIN]->u.fs_d.value);
  desc.model.global_tone = static_cast<float>(params[AEX_GLOBAL_TONE]->u.fs_d.value);
  // Motion controls remain in the compatible UI, but this engine uses dummy zero MVec.
  desc.model.preset = params[AEX_PRESET]->u.sd.value;
  desc.hdr = params[AEX_HDR]->u.bd.value != FALSE;
  desc.model.auto_mask = params[AEX_AUTO_MASK]->u.bd.value != FALSE;
  desc.runtime_dir = runtime.c_str();
  desc.in_rgba = packed.data();
  desc.out_rgba = outPacked.data();
  Logf("aex render: nr_eval %dx%d request=%u:%u intensity=%.2f style=%u srcfmt=%d dstfmt=%d",
       (int)w, (int)h, desc.instance_id, desc.request_id, desc.model.intensity,
       desc.model.style, (int)inputFormat, (int)outputFormat);

  nr_status status = engine_client::Eval(desc);
  if (Aborted(in_data)) {
    engine_client::Cancel(desc.instance_id, desc.request_id);
    status = NR_ERR_SUPERSEDED;
  }
  if (status == NR_OK) {
    pix::UnpackRgba8(dst, outPacked, /*flipVert=*/false);
    const bool protect = params[AEX_HL_PROTECT]->u.bd.value != FALSE;
    const bool showMask = params[AEX_HL_SHOW_MASK]->u.bd.value != FALSE;
    if (protect || showMask) {
      pix::RestoreHighlights(src, dst, static_cast<float>(params[AEX_HL_THRESHOLD]->u.fs_d.value),
                             static_cast<float>(params[AEX_HL_SOFTNESS]->u.fs_d.value),
                             static_cast<float>(params[AEX_HL_MIX]->u.fs_d.value), showMask);
    }
    Logf("aex render: nr_eval OK request=%u:%u", desc.instance_id, desc.request_id);
  } else {
    Logf("aex render: nr_eval %s request=%u:%u; passthrough",
         engine_client::StatusName(status), desc.instance_id, desc.request_id);
    pix::CopyPassthrough(src, dst);
  }
  return err;
}

PF_Err EntryPoint(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
                  PF_LayerDef* output, void* /*extra*/) {
  PF_Err err = PF_Err_NONE;
  switch (cmd) {
    case PF_Cmd_GLOBAL_SETUP:
      err = GlobalSetup(in_data, out_data);
      break;
    case PF_Cmd_PARAMS_SETUP:
      err = ParamsSetup(in_data, out_data);
      break;
    case PF_Cmd_GLOBAL_SETDOWN:
      Logf("aex global setdown: shutting down in-process engine");
      engine_client::Shutdown();
      break;
    case PF_Cmd_RENDER:
      err = Render(in_data, out_data, params, output);
      break;
    default:
      break;
  }
  return err;
}

}  // namespace

extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                                       PF_ParamDef* params[], PF_LayerDef* output, void* extra) {
  return EntryPoint(cmd, in_data, out_data, params, output, extra);
}

extern "C" DllExport PF_Err PluginDataEntryFunction2(
    PF_PluginDataPtr inPtr, PF_PluginDataCB2 inPluginDataCallBackPtr, SPBasicSuite* /*inSPBasicSuitePtr*/,
    const char* /*inHostName*/, const char* /*inHostVersion*/) {
  PF_Err result = PF_Err_INVALID_CALLBACK;
  result = PF_REGISTER_EFFECT_EXT2(inPtr, inPluginDataCallBackPtr, "DLSS Neural Rendering",
                                   "ADBE DLSS Neural Rendering", "Filter", AE_RESERVED_INFO, "EffectMain", "");
  return result;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID /*reserved*/) {
  if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(hinst);
  // No logging, waits, GPU work, or engine shutdown under the loader lock.
  return TRUE;
}