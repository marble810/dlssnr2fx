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
  AEX_VIEW,
  AEX_GRP_LOOK,
  AEX_INTENSITY,
  AEX_STYLE,
  AEX_DETAIL,
  AEX_COLOR,
  AEX_UI_CORRECTION,
  AEX_GRP_LOOK_END,
  AEX_GRP_STRUCTURE,
  AEX_LOCAL_STRUCTURE,
  AEX_LOCAL_TONE,
  AEX_SKIN,
  AEX_GLOBAL_TONE,
  AEX_GRP_STRUCTURE_END,
  AEX_MOTION,
  AEX_MOTION_ENGINE,
  AEX_MOTION_VIS,
  AEX_GRP_HIGHLIGHT,
  AEX_HL_PROTECT,
  AEX_HL_THRESHOLD,
  AEX_HL_SOFTNESS,
  AEX_HL_MIX,
  AEX_HL_SHOW_MASK,
  AEX_GRP_HIGHLIGHT_END,
  AEX_GRP_MVEC,
  AEX_MVEC_SOURCE,
  AEX_MVEC_LAYER,
  AEX_MVEC_DIR,
  AEX_MVEC_UNIT,
  AEX_MVEC_ENC,
  AEX_MVEC_RANGE,
  AEX_NVOF_QUALITY,
  AEX_NVOF_HINT,
  AEX_GRP_MVEC_END,
  AEX_GRP_DEPTH,
  AEX_DEPTH_SOURCE,
  AEX_DEPTH_LAYER,
  AEX_DEPTH_CONV,
  AEX_DAV2_INTERVAL,
  AEX_DAV2_EMA,
  AEX_GRP_DEPTH_END,
  AEX_GRP_FG,
  AEX_FG_MODE,
  AEX_FG_COALESCE,
  AEX_GRP_FG_END,
  AEX_GRP_ADVANCED,
  AEX_PRESET,
  AEX_HDR,
  AEX_AUTO_MASK,
  AEX_ADAPTER,
  AEX_GRP_ADVANCED_END,
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

  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("View", 5, 1, "Final Image|Source Image|Depth Map|Motion Vector Map|Highlight Mask",
               AEX_VIEW);

  AEFX_CLR_STRUCT(def);
  PF_ADD_TOPIC("Look", AEX_GRP_LOOK);
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
  AEFX_CLR_STRUCT(def);
  PF_END_TOPIC(AEX_GRP_LOOK_END);

  AEFX_CLR_STRUCT(def);
  PF_ADD_TOPIC("Structure", AEX_GRP_STRUCTURE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Local Structure", 0, 2, 0, 2, 1.0, 2, PF_ValueDisplayFlag_NONE, 0, AEX_LOCAL_STRUCTURE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Local Tone", 0, 2, 0, 2, 1.0, 2, PF_ValueDisplayFlag_NONE, 0, AEX_LOCAL_TONE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Skin", -1, 2, -1, 2, -1.0, 2, PF_ValueDisplayFlag_NONE, 0, AEX_SKIN);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Global Tone", -1, 2, -1, 2, -1.0, 2, PF_ValueDisplayFlag_NONE, 0, AEX_GLOBAL_TONE);
  AEFX_CLR_STRUCT(def);
  PF_END_TOPIC(AEX_GRP_STRUCTURE_END);

  AEFX_CLR_STRUCT(def);
  def.ui_flags = PF_PUI_INVISIBLE;
  PF_ADD_CHECKBOX("Motion Vectors", "", FALSE, 0, AEX_MOTION);
  AEFX_CLR_STRUCT(def);
  def.ui_flags = PF_PUI_INVISIBLE;
  PF_ADD_POPUP("Motion Engine", 3, 1, "Auto|NVOF|Lucas-Kanade", AEX_MOTION_ENGINE);
  AEFX_CLR_STRUCT(def);
  def.ui_flags = PF_PUI_INVISIBLE;
  PF_ADD_CHECKBOX("Motion Visualize", "", FALSE, 0, AEX_MOTION_VIS);

  AEFX_CLR_STRUCT(def);
  PF_ADD_TOPIC("Highlight Protect", AEX_GRP_HIGHLIGHT);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("Enable", TRUE, 0, AEX_HL_PROTECT);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Threshold", 0, 1, 0, 1, 0.80, 2, PF_ValueDisplayFlag_NONE, 0,
                       AEX_HL_THRESHOLD);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Softness", 0, 1, 0, 1, 0.15, 2, PF_ValueDisplayFlag_NONE, 0,
                       AEX_HL_SOFTNESS);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Mix", 0, 1, 0, 1, 1.0, 2, PF_ValueDisplayFlag_NONE, 0, AEX_HL_MIX);
  AEFX_CLR_STRUCT(def);
  def.ui_flags = PF_PUI_INVISIBLE;
  PF_ADD_CHECKBOXX("Show Mask", FALSE, 0, AEX_HL_SHOW_MASK);
  AEFX_CLR_STRUCT(def);
  PF_END_TOPIC(AEX_GRP_HIGHLIGHT_END);

  AEFX_CLR_STRUCT(def);
  PF_ADD_TOPIC("Motion Vectors", AEX_GRP_MVEC);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("Source", 3, 3, "Off|Input|Estimate (NVOF)", AEX_MVEC_SOURCE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_LAYER("Input", 0, AEX_MVEC_LAYER);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("Direction", 2, 1, "Current to Previous|Previous to Current", AEX_MVEC_DIR);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("Units", 2, 1, "Pixels|UV", AEX_MVEC_UNIT);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("Encoding", 2, 1, "Centered 0.5 (AE)|Signed 0=still", AEX_MVEC_ENC);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Range (px)", 1, 1024, 1, 1024, 64.0, 1, PF_ValueDisplayFlag_NONE, 0,
                       AEX_MVEC_RANGE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("NVOF Quality", 3, 2, "Slow|Medium|Fast", AEX_NVOF_QUALITY);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("NVOF Temporal Hint", TRUE, 0, AEX_NVOF_HINT);
  AEFX_CLR_STRUCT(def);
  PF_END_TOPIC(AEX_GRP_MVEC_END);

  AEFX_CLR_STRUCT(def);
  PF_ADD_TOPIC("Depth", AEX_GRP_DEPTH);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("Source", 3, 3, "Off|Input|Estimate (DAV2)", AEX_DEPTH_SOURCE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_LAYER("Input", 0, AEX_DEPTH_LAYER);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("Convention", 2, 1, "Inverse (near=1)|Linear (near=0)", AEX_DEPTH_CONV);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("DAV2 Interval", 1, 8, 1, 8, 4, AEX_DAV2_INTERVAL);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("DAV2 Temporal Blend", 0, 0.95, 0, 0.95, 0.70, 2, PF_ValueDisplayFlag_NONE, 0,
                       AEX_DAV2_EMA);
  AEFX_CLR_STRUCT(def);
  PF_END_TOPIC(AEX_GRP_DEPTH_END);

  AEFX_CLR_STRUCT(def);
  PF_ADD_TOPIC("Frame Guidance", AEX_GRP_FG);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("Mode", 2, 2, "Always Reset|Lock History", AEX_FG_MODE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("Coalesce Same Frame", TRUE, 0, AEX_FG_COALESCE);
  AEFX_CLR_STRUCT(def);
  PF_END_TOPIC(AEX_GRP_FG_END);

  AEFX_CLR_STRUCT(def);
  PF_ADD_TOPIC("Advanced", AEX_GRP_ADVANCED);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("NR Preset", 0, 8, 0, 8, 0, AEX_PRESET);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("HDR / Linear", FALSE, 0, AEX_HDR);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOXX("Auto Mask", FALSE, 0, AEX_AUTO_MASK);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("GPU Adapter", -1, 8, -1, 8, -1, AEX_ADAPTER);
  AEFX_CLR_STRUCT(def);
  PF_END_TOPIC(AEX_GRP_ADVANCED_END);

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

  const int view = params[AEX_VIEW]->u.pd.value > 0 ? params[AEX_VIEW]->u.pd.value - 1 : 0;
  if (view == 1) {
    Logf("aex render: view source %dx%d", (int)w, (int)h);
    return passthrough();
  }
  if (view == 4) {
    pix::CopyPassthrough(src, dst);
    pix::RestoreHighlights(src, dst, static_cast<float>(params[AEX_HL_THRESHOLD]->u.fs_d.value),
                           static_cast<float>(params[AEX_HL_SOFTNESS]->u.fs_d.value),
                           static_cast<float>(params[AEX_HL_MIX]->u.fs_d.value), true);
    return PF_Err_NONE;
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

  std::vector<uint8_t> packed8, out8;
  std::vector<float> packedF, outF;
  const bool deep = pix::PackAsFloat(src);
  // PF_EffectWorld rows are top-to-bottom; no vertical conversion is needed.
  if (deep) {
    pix::PackRgba32f(src, packedF, /*flipVert=*/false);
    outF.resize(packedF.size());
  } else {
    pix::PackRgba8(src, packed8, /*flipVert=*/false);
    out8.resize(packed8.size());
  }
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
  desc.model.preset = params[AEX_PRESET]->u.sd.value;
  desc.hdr = params[AEX_HDR]->u.bd.value != FALSE;
  desc.model.auto_mask = params[AEX_AUTO_MASK]->u.bd.value != FALSE;
  desc.runtime_dir = runtime.c_str();
  desc.format = deep ? NR_PIX_RGBA32F : NR_PIX_RGBA8;
  desc.frame_id = in_data->current_time;
  desc.frame_step = in_data->time_step > 0 ? in_data->time_step : 0;
  {
    const int mode = params[AEX_FG_MODE]->u.pd.value;
    desc.fg_mode = (mode <= 1) ? NR_FG_ALWAYS_RESET : NR_FG_LOCK;
  }
  desc.fg_coalesce = params[AEX_FG_COALESCE]->u.bd.value != FALSE;
  {
    const int srcMode = params[AEX_MVEC_SOURCE]->u.pd.value;
    desc.mvec_source = static_cast<nr_guide_source>(srcMode > 0 ? srcMode - 1 : 0);
  }
  {
    const int q = params[AEX_NVOF_QUALITY]->u.pd.value;
    desc.nvof_quality = static_cast<nr_nvof_quality>(q > 0 ? q - 1 : 1);
  }
  desc.nvof_hint = params[AEX_NVOF_HINT]->u.bd.value != FALSE;
  {
    const int srcMode = params[AEX_DEPTH_SOURCE]->u.pd.value;
    desc.depth_source = static_cast<nr_guide_source>(srcMode > 0 ? srcMode - 1 : 0);
  }
  desc.dav2_interval = params[AEX_DAV2_INTERVAL]->u.sd.value;
  desc.dav2_ema = static_cast<float>(params[AEX_DAV2_EMA]->u.fs_d.value);
  desc.in_rgba = deep ? static_cast<const void*>(packedF.data()) : packed8.data();
  desc.out_rgba = deep ? static_cast<void*>(outF.data()) : out8.data();
  std::vector<uint8_t> prev8;
  std::vector<float> prevF;
  PF_ParamDef prevParam;
  AEFX_CLR_STRUCT(prevParam);
  bool prevChecked = false;
  if (in_data->time_step > 0 && in_data->current_time >= in_data->time_step &&
      PF_CHECKOUT_PARAM(in_data, AEX_INPUT, in_data->current_time - in_data->time_step,
                        in_data->time_step, in_data->time_scale, &prevParam) == PF_Err_NONE) {
    prevChecked = true;
    if (prevParam.u.ld.data) {
      PF_PixelFormat prevFmt = inputFormat;
      {
        AEFX_SuiteScoper<PF_WorldSuite2> ws(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
        if (ws->PF_GetPixelFormat(&prevParam.u.ld, &prevFmt) != PF_Err_NONE)
          prevFmt = inputFormat;
      }
      pix::PixWorld prevWorld = MakeAexWorld(&prevParam.u.ld, prevFmt);
      if (prevWorld.width == w && prevWorld.height == h && prevWorld.bytesPerComp == src.bytesPerComp) {
        if (deep) {
          pix::PackRgba32f(prevWorld, prevF, /*flipVert=*/false);
          desc.in_prev_rgba = prevF.data();
        } else {
          pix::PackRgba8(prevWorld, prev8, /*flipVert=*/false);
          desc.in_prev_rgba = prev8.data();
        }
      }
    }
  }
  std::vector<float> guideMvec, guideDepth;
  PF_ParamDef mvecParam, depthParam;
  AEFX_CLR_STRUCT(mvecParam);
  AEFX_CLR_STRUCT(depthParam);
  bool mvecChecked = false, depthChecked = false;
  if (desc.mvec_source == NR_GUIDE_INPUT &&
      PF_CHECKOUT_PARAM(in_data, AEX_MVEC_LAYER, in_data->current_time, in_data->time_step,
                        in_data->time_scale, &mvecParam) == PF_Err_NONE) {
    mvecChecked = true;
    if (mvecParam.u.ld.data) {
      PF_PixelFormat fmt = inputFormat;
      {
        AEFX_SuiteScoper<PF_WorldSuite2> ws(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
        if (ws->PF_GetPixelFormat(&mvecParam.u.ld, &fmt) != PF_Err_NONE) fmt = inputFormat;
      }
      pix::PixWorld world = MakeAexWorld(&mvecParam.u.ld, fmt);
      if (world.width == w && world.height == h) {
        pix::PackMvec(world, guideMvec, false, params[AEX_MVEC_DIR]->u.pd.value > 1,
                      params[AEX_MVEC_UNIT]->u.pd.value > 1,
                      params[AEX_MVEC_ENC]->u.pd.value <= 1,
                      static_cast<float>(params[AEX_MVEC_RANGE]->u.fs_d.value));
        desc.in_mvec = guideMvec.data();
      }
    }
  }
  if (desc.depth_source == NR_GUIDE_INPUT &&
      PF_CHECKOUT_PARAM(in_data, AEX_DEPTH_LAYER, in_data->current_time, in_data->time_step,
                        in_data->time_scale, &depthParam) == PF_Err_NONE) {
    depthChecked = true;
    if (depthParam.u.ld.data) {
      PF_PixelFormat fmt = inputFormat;
      {
        AEFX_SuiteScoper<PF_WorldSuite2> ws(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
        if (ws->PF_GetPixelFormat(&depthParam.u.ld, &fmt) != PF_Err_NONE) fmt = inputFormat;
      }
      pix::PixWorld world = MakeAexWorld(&depthParam.u.ld, fmt);
      if (world.width == w && world.height == h) {
        pix::PackDepth(world, guideDepth, false, params[AEX_DEPTH_CONV]->u.pd.value > 1);
        desc.in_depth = guideDepth.data();
      }
    }
  }
  const size_t pixels = static_cast<size_t>(w) * h;
  std::vector<float> usedMvec(pixels * 2, 0.f), usedDepth(pixels, 0.f);
  desc.out_mvec = usedMvec.data();
  desc.out_depth = usedDepth.data();
  desc.maps_only = (view == 2 || view == 3);
  Logf("aex render: nr_eval %dx%d request=%u:%u intensity=%.2f style=%u srcfmt=%d dstfmt=%d format=%s frame=%lld step=%lld fg=%d coalesce=%d prev=%d mvec=%d z=%d view=%d",
       (int)w, (int)h, desc.instance_id, desc.request_id, desc.model.intensity,
       desc.model.style, (int)inputFormat, (int)outputFormat, deep ? "RGBA32F" : "RGBA8",
       static_cast<long long>(desc.frame_id), static_cast<long long>(desc.frame_step),
       static_cast<int>(desc.fg_mode), desc.fg_coalesce, desc.in_prev_rgba ? 1 : 0,
       desc.in_mvec ? 1 : 0, desc.in_depth ? 1 : 0, view);

  nr_status status = engine_client::Eval(desc);
  if (prevChecked)
    PF_CHECKIN_PARAM(in_data, &prevParam);
  if (mvecChecked)
    PF_CHECKIN_PARAM(in_data, &mvecParam);
  if (depthChecked)
    PF_CHECKIN_PARAM(in_data, &depthParam);
  if (Aborted(in_data)) {
    engine_client::Cancel(desc.instance_id, desc.request_id);
    status = NR_ERR_SUPERSEDED;
  }
  if (status == NR_OK) {
    if (view == 2) {
      pix::VizDepth(dst, usedDepth.data(), false, pixels);
    } else if (view == 3) {
      pix::VizMvec(dst, usedMvec.data(), false, pixels);
    } else {
      if (deep) pix::UnpackRgba32f(dst, outF, /*flipVert=*/false);
      else pix::UnpackRgba8(dst, out8, /*flipVert=*/false);
      const bool protect = params[AEX_HL_PROTECT]->u.bd.value != FALSE;
      if (protect) {
        pix::RestoreHighlights(src, dst, static_cast<float>(params[AEX_HL_THRESHOLD]->u.fs_d.value),
                               static_cast<float>(params[AEX_HL_SOFTNESS]->u.fs_d.value),
                               static_cast<float>(params[AEX_HL_MIX]->u.fs_d.value), false);
      }
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