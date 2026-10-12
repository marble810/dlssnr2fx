// Exercises the real entry point with Adobe SDK types and a CPU-only fake host.
// Replays the AE UI/render value split captured by aex_ui_smoke.jsx.
#include "../plugin/aex/dlssnr_aex.cpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>

namespace {
std::vector<PF_ParamDef> gParams;
std::vector<PF_ParamDef*> gCheckouts;
std::vector<float> gMvec, gDepth;
nr_eval_desc gEval{};
int gEvalCalls = 0, gFailIndex = -1;
bool gBadTime = false, gBadCheckin = false;

PF_Err AddParam(PF_ProgPtr, PF_ParamIndex index, PF_ParamDefPtr def) {
  if (index != -1 || def->uu.id != static_cast<A_long>(gParams.size()))
    return PF_Err_BAD_CALLBACK_PARAM;
  gParams.push_back(*def);
  return PF_Err_NONE;
}
PF_Err Abort(PF_ProgPtr) { return PF_Err_NONE; }
PF_Err Checkout(PF_ProgPtr, PF_ParamIndex index, A_long time, A_long step,
                A_u_long scale, PF_ParamDef* def) {
  if (index == gFailIndex || index <= 0 || index >= static_cast<PF_ParamIndex>(gParams.size()))
    return PF_Err_BAD_CALLBACK_PARAM;
  if (time != 137 || step != 0 || scale != 24) gBadTime = true;
  *def = gParams[index];
  gCheckouts.push_back(def);
  return PF_Err_NONE;
}
PF_Err Checkin(PF_ProgPtr, PF_ParamDef* def) {
  auto found = std::find(gCheckouts.begin(), gCheckouts.end(), def);
  if (found == gCheckouts.end()) gBadCheckin = true;
  else gCheckouts.erase(found);
  // The host owns checked-out values and invalidates them on checkin.
  std::memset(def, 0, sizeof(*def));
  return PF_Err_NONE;
}
PF_Err PixelFormat(const PF_EffectWorld* world, PF_PixelFormat* format) {
  *format = (world->world_flags & PF_WorldFlag_DEEP) ? PF_PixelFormat_ARGB64 : PF_PixelFormat_ARGB32;
  return PF_Err_NONE;
}
PF_WorldSuite2 gWorldSuite{};
SPErr SPAPI Acquire(const char*, int32, const void** suite) {
  *suite = &gWorldSuite;
  return kSPNoError;
}
SPErr SPAPI Release(const char*, int32) { return kSPNoError; }
int Fail(const char* what) {
  std::fprintf(stderr, "FAIL: %s\n", what);
  return 1;
}
bool Near(float a, float b) { return std::abs(a - b) < .0001f; }
}  // namespace

void LogInit() {}
void Logf(const char*, ...) {}
bool EnvTruthy(const wchar_t*) { return false; }
std::wstring FindRuntimeDir() { return L"fake-runtime"; }
namespace engine_client {
nr_status Eval(const nr_eval_desc& desc) {
  gEval = desc;
  ++gEvalCalls;
  const size_t count = static_cast<size_t>(desc.width) * desc.height;
  gMvec.clear(); gDepth.clear();
  if (desc.in_mvec) gMvec.assign(desc.in_mvec, desc.in_mvec + count * 2);
  if (desc.in_depth) gDepth.assign(desc.in_depth, desc.in_depth + count);
  std::fill_n(desc.out_depth, count, .25f);
  std::fill_n(desc.out_mvec, count * 2, 2.f);
  // A black NR result makes highlight restoration observable independently.
  if (!desc.maps_only) {
    if (desc.format == NR_PIX_RGBA32F) {
      auto out = static_cast<float*>(desc.out_rgba);
      const auto in = static_cast<const float*>(desc.in_rgba);
      for (size_t i = 0; i < count; ++i) {
        out[i * 4] = out[i * 4 + 1] = out[i * 4 + 2] = 0;
        out[i * 4 + 3] = in[i * 4 + 3];
      }
    } else {
      auto out = static_cast<uint8_t*>(desc.out_rgba);
      const auto in = static_cast<const uint8_t*>(desc.in_rgba);
      for (size_t i = 0; i < count; ++i) {
        out[i * 4] = out[i * 4 + 1] = out[i * 4 + 2] = 0;
        out[i * 4 + 3] = in[i * 4 + 3];
      }
    }
  }
  return NR_OK;
}
void Cancel(uint32_t, uint32_t) {}
void Shutdown() {}
const char* StatusName(nr_status) { return "stub"; }
}  // namespace engine_client

int main() {
  PF_InData in{};
  PF_OutData out{};
  SPBasicSuite basic{};
  basic.AcquireSuite = Acquire;
  basic.ReleaseSuite = Release;
  gWorldSuite.PF_GetPixelFormat = PixelFormat;
  in.pica_basicP = &basic;
  in.inter.add_param = AddParam;
  in.inter.abort = Abort;
  in.inter.checkout_param = Checkout;
  in.inter.checkin_param = Checkin;
  in.current_time = 137; in.time_step = 0; in.time_scale = 24;
  gParams.resize(1);
  gParams[0].param_type = PF_Param_LAYER;
  if (EffectMain(PF_Cmd_PARAMS_SETUP, &in, &out, nullptr, nullptr, nullptr) != PF_Err_NONE ||
      out.num_params != AEX_NUM_PARAMS || gParams.size() != AEX_NUM_PARAMS)
    return Fail("ParamsSetup registers every parameter at its render index");
  if (!(gParams[AEX_HL_SHOW_MASK].ui_flags & PF_PUI_INVISIBLE))
    return Fail("legacy Show Mask must stay hidden (CHECKBOXX clears ui_flags)");

  PF_Pixel source[] = {{255, 240, 30, 10}, {255, 10, 20, 30}};
  PF_Pixel output[2]{};
  auto& input = gParams[0].u.ld;
  input.data = source; input.width = 2; input.height = 1; input.rowbytes = sizeof(source);
  PF_LayerDef dest = input;
  dest.data = output;
  // Checkout represents current values; the supplied render array is stale.
  auto renderSnapshot = gParams;
  std::vector<PF_ParamDef*> params;
  for (auto& p : renderSnapshot) params.push_back(&p);
  params.push_back(nullptr);
  auto render = [&]() {
    const PF_Err err = EffectMain(PF_Cmd_RENDER, &in, &out, params.data(), &dest, nullptr);
    if (!gCheckouts.empty() || gBadCheckin || gBadTime)
      return static_cast<PF_Err>(PF_Err_INTERNAL_STRUCT_DAMAGED);
    return err;
  };

  gParams[AEX_VIEW].u.pd.value = 5;
  if (render() != PF_Err_NONE) return Fail("Highlight Mask render and balanced checkouts");
  if (gEvalCalls || output[0].red == 0 || output[0].red != output[0].green ||
      output[0].green != output[0].blue || output[1].red != 0 || output[1].green != 0 || output[1].blue != 0)
    return Fail("View=Highlight Mask must output a grayscale mask, not default NR/source pixels");
  gParams[AEX_HL_THRESHOLD].u.fs_d.value = 1;
  if (render() != PF_Err_NONE || output[0].red != 0)
    return Fail("Highlight Mask reacts to threshold on the same frame");
  gParams[AEX_HL_THRESHOLD].u.fs_d.value = 0;
  gParams[AEX_HL_SOFTNESS].u.fs_d.value = 0;
  gParams[AEX_HL_MIX].u.fs_d.value = .5;
  if (render() != PF_Err_NONE || output[0].red != 128 || output[1].red != 128)
    return Fail("Highlight Mask reacts to softness/mix on the same frame");

  gParams[AEX_VIEW].u.pd.value = 2;
  if (render() != PF_Err_NONE || gEvalCalls || std::memcmp(source, output, sizeof(source)))
    return Fail("Source Image bypass and balanced checkouts");
  gParams[AEX_VIEW].u.pd.value = 1;
  gParams[AEX_INTENSITY].u.fs_d.value = .37;
  gParams[AEX_STYLE].u.pd.value = 3;
  gParams[AEX_DETAIL].u.fs_d.value = .23;
  gParams[AEX_COLOR].u.fs_d.value = .41;
  gParams[AEX_UI_CORRECTION].u.bd.value = TRUE;
  gParams[AEX_LOCAL_STRUCTURE].u.fs_d.value = .7;
  gParams[AEX_LOCAL_TONE].u.fs_d.value = .8;
  gParams[AEX_SKIN].u.fs_d.value = .9;
  gParams[AEX_GLOBAL_TONE].u.fs_d.value = .6;
  gParams[AEX_PRESET].u.sd.value = 4;
  gParams[AEX_HDR].u.bd.value = TRUE;
  gParams[AEX_AUTO_MASK].u.bd.value = TRUE;
  gParams[AEX_ADAPTER].u.sd.value = 2;
  gParams[AEX_HL_PROTECT].u.bd.value = FALSE;
  gParams[AEX_FG_MODE].u.pd.value = 1;
  gParams[AEX_FG_COALESCE].u.bd.value = FALSE;
  gParams[AEX_MVEC_SOURCE].u.pd.value = 1;
  gParams[AEX_DEPTH_SOURCE].u.pd.value = 1;
  gParams[AEX_NVOF_QUALITY].u.pd.value = 3;
  gParams[AEX_NVOF_HINT].u.bd.value = FALSE;
  gParams[AEX_DAV2_INTERVAL].u.sd.value = 7;
  gParams[AEX_DAV2_EMA].u.fs_d.value = .2;
  if (render() != PF_Err_NONE) return Fail("Final Image render");
  if (gEvalCalls != 1 || !Near(gEval.model.intensity, .37f) || gEval.model.style != 2 ||
      !Near(gEval.detail, .23f) || !Near(gEval.color, .41f) || !gEval.model.ui_correction ||
      !Near(gEval.model.local_structure, .7f) || !Near(gEval.model.local_tone, .8f) ||
      !Near(gEval.model.skin, .9f) || !Near(gEval.model.global_tone, .6f) ||
      gEval.model.preset != 4 || !gEval.hdr || !gEval.model.auto_mask || gEval.adapter != 2 ||
      gEval.fg_mode != NR_FG_ALWAYS_RESET || gEval.fg_coalesce || gEval.nvof_quality != NR_NVOF_FAST ||
      gEval.nvof_hint || gEval.dav2_interval != 7 || !Near(gEval.dav2_ema, .2f) ||
      gEval.mvec_source != NR_GUIDE_OFF || gEval.depth_source != NR_GUIDE_OFF)
    return Fail("all UI model/composite/guidance/advanced controls must reach Eval unchanged");
  if (output[0].red != 0) return Fail("Highlight Protect enable must use live value, not default true");
  gParams[AEX_HL_PROTECT].u.bd.value = TRUE;
  gParams[AEX_HL_MIX].u.fs_d.value = 1;
  if (render() != PF_Err_NONE || std::memcmp(source, output, sizeof(source)))
    return Fail("live highlight protection restores source with threshold=0/mix=1");

  gParams[AEX_VIEW].u.pd.value = 3;
  gParams[AEX_MVEC_SOURCE].u.pd.value = 2;
  gParams[AEX_MVEC_LAYER].u.ld = input;
  gParams[AEX_MVEC_DIR].u.pd.value = 2;
  gParams[AEX_MVEC_UNIT].u.pd.value = 2;
  gParams[AEX_MVEC_ENC].u.pd.value = 2;
  gParams[AEX_MVEC_RANGE].u.fs_d.value = 32;
  gParams[AEX_DEPTH_SOURCE].u.pd.value = 2;
  gParams[AEX_DEPTH_LAYER].u.ld = input;
  gParams[AEX_DEPTH_CONV].u.pd.value = 2;
  if (render() != PF_Err_NONE || !gEval.maps_only || gMvec.size() != 4 || gDepth.size() != 2 ||
      !Near(gMvec[0], -(240.f / 255.f - .5f) * 4) || !Near(gDepth[0], 1 - 240.f / 255.f) ||
      output[0].red != 64 || output[0].green != 64 || output[0].blue != 64)
    return Fail("Depth Map and separately checked-out guide layers use live conversions");
  gParams[AEX_VIEW].u.pd.value = 4;
  if (render() != PF_Err_NONE || !gEval.maps_only || std::memcmp(source, output, sizeof(source)) == 0)
    return Fail("Motion Vector Map must not output source/Final Image");
  gParams[AEX_VIEW].u.pd.value = 1;
  gParams[AEX_HL_SHOW_MASK].u.bd.value = TRUE;
  const int calls = gEvalCalls;
  if (render() != PF_Err_NONE || gEvalCalls != calls || output[0].red != 255 || output[0].green != 255)
    return Fail("legacy Show Mask values remain effective without NR");

  gFailIndex = AEX_STYLE;
  if (render() != PF_Err_BAD_CALLBACK_PARAM || gEvalCalls != calls)
    return Fail("partial checkout failure returns host error and checks in earlier values");
  gFailIndex = -1;
  PF_Pixel16 deepSource[] = {{PF_MAX_CHAN16, PF_MAX_CHAN16, 100, 0}, {PF_MAX_CHAN16, 0, 0, 0}};
  PF_Pixel16 deepOutput[2]{};
  renderSnapshot[0].u.ld.data = reinterpret_cast<PF_Pixel*>(deepSource);
  renderSnapshot[0].u.ld.rowbytes = sizeof(deepSource);
  renderSnapshot[0].u.ld.world_flags = PF_WorldFlag_DEEP;
  dest = renderSnapshot[0].u.ld;
  dest.data = reinterpret_cast<PF_Pixel*>(deepOutput);
  gParams[AEX_VIEW].u.pd.value = 5;
  gParams[AEX_HL_THRESHOLD].u.fs_d.value = .8;
  gParams[AEX_HL_SOFTNESS].u.fs_d.value = .15;
  if (render() != PF_Err_NONE || gEvalCalls != calls || deepOutput[0].red != PF_MAX_CHAN16 ||
      deepOutput[0].green != PF_MAX_CHAN16 || deepOutput[1].red != 0)
    return Fail("16-bit AE Highlight Mask keeps the real supplied input world");
  std::puts("PASS: AE live parameter checkout, all Views/controls, guide layers, 8/16-bit, cleanup/errors");
  return 0;
}
