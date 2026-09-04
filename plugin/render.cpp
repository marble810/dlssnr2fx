#include "plugin.h"

#include "engine_client.h"
#include "log.h"
#include "pixio.h"
#include "runtime.h"

#include <cstring>
#include <string>
#include <vector>

namespace {
struct ImageInfo : pix::PixWorld {
  OfxPropertySetHandle props = nullptr;
  const char* depth = kOfxBitDepthByte;
};

int DepthBytes(const char* depth) {
  if (depth && strcmp(depth, kOfxBitDepthFloat) == 0) return 4;
  if (depth && strcmp(depth, kOfxBitDepthShort) == 0) return 2;
  return 1;
}
int CompCount(const char* comps) {
  if (comps && strcmp(comps, kOfxImageComponentAlpha) == 0) return 1;
  if (comps && strcmp(comps, kOfxImageComponentRGB) == 0) return 3;
  return 4;
}
bool FillImage(OfxPropertySetHandle props, ImageInfo& img) {
  img.props = props;
  void* ptr = nullptr;
  gPropHost->propGetPointer(props, kOfxImagePropData, 0, &ptr);
  img.data = static_cast<uint8_t*>(ptr);
  gPropHost->propGetInt(props, kOfxImagePropRowBytes, 0, &img.rowBytes);
  gPropHost->propGetIntN(props, kOfxImagePropBounds, 4, &img.bounds.x1);
  img.width = img.bounds.x2 - img.bounds.x1;
  img.height = img.bounds.y2 - img.bounds.y1;
  char* depth = nullptr;
  char* comps = nullptr;
  gPropHost->propGetString(props, kOfxImageEffectPropPixelDepth, 0, &depth);
  gPropHost->propGetString(props, kOfxImageEffectPropComponents, 0, &comps);
  img.depth = depth ? depth : kOfxBitDepthByte;
  img.bytesPerComp = DepthBytes(img.depth);
  img.nComp = CompCount(comps);
  return img.data && img.width > 0 && img.height > 0;
}
}  // namespace

void FillNrParams(InstanceData* data, nr_eval_desc& desc) {
  double d = 1.0;
  int i = 0;
  gParamHost->paramGetValue(data->intensityParam, &d); desc.model.intensity = static_cast<float>(d);
  gParamHost->paramGetValue(data->styleParam, &i); desc.model.style = static_cast<uint32_t>(i);
  gParamHost->paramGetValue(data->detailParam, &d); desc.detail = static_cast<float>(d);
  gParamHost->paramGetValue(data->colorParam, &d); desc.color = static_cast<float>(d);
  gParamHost->paramGetValue(data->uiParam, &i); desc.model.ui_correction = i != 0;
  gParamHost->paramGetValue(data->localStructureParam, &d); desc.model.local_structure = static_cast<float>(d);
  gParamHost->paramGetValue(data->localToneParam, &d); desc.model.local_tone = static_cast<float>(d);
  gParamHost->paramGetValue(data->skinParam, &d); desc.model.skin = static_cast<float>(d);
  gParamHost->paramGetValue(data->globalToneParam, &d); desc.model.global_tone = static_cast<float>(d);
  gParamHost->paramGetValue(data->presetParam, &i); desc.model.preset = static_cast<uint32_t>(i);
  gParamHost->paramGetValue(data->hdrParam, &i); desc.hdr = i != 0;
  gParamHost->paramGetValue(data->autoMaskParam, &i); desc.model.auto_mask = i != 0;
  gParamHost->paramGetValue(data->adapterParam, &i); desc.adapter = i;
  // Motion UI is retained for compatibility, but feature 18 v1 intentionally uses dummy zero MVec.
}

OfxStatus Render(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs,
                 OfxPropertySetHandle /*outArgs*/) {
  InstanceData* data = GetInstanceData(effect);
  if (!data) return kOfxStatFailed;
  double time = 0;
  gPropHost->propGetDouble(inArgs, kOfxPropTime, 0, &time);
  OfxRectI renderWin{};
  gPropHost->propGetIntN(inArgs, kOfxImageEffectPropRenderWindow, 4, &renderWin.x1);
  double scale[2] = {1.0, 1.0};
  gPropHost->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, scale);

  OfxPropertySetHandle srcImg = nullptr, dstImg = nullptr;
  OfxStatus st = gEffectHost->clipGetImage(data->sourceClip, time, nullptr, &srcImg);
  if (st != kOfxStatOK) return st;
  st = gEffectHost->clipGetImage(data->outputClip, time, nullptr, &dstImg);
  if (st != kOfxStatOK) { gEffectHost->clipReleaseImage(srcImg); return st; }
  ImageInfo src{}, dst{};
  if (!FillImage(srcImg, src) || !FillImage(dstImg, dst)) {
    Logf("render: missing image data");
    gEffectHost->clipReleaseImage(srcImg); gEffectHost->clipReleaseImage(dstImg);
    return kOfxStatFailed;
  }
  auto finish = [&](bool copy) {
    if (copy) pix::CopyPassthrough(src, dst);
    gEffectHost->clipReleaseImage(srcImg); gEffectHost->clipReleaseImage(dstImg);
    return kOfxStatOK;
  };
  auto aborted = [&]() { return gEffectHost->abort && gEffectHost->abort(effect); };
  int interactive = 0;
  gPropHost->propGetInt(inArgs, kOfxImageEffectPropInteractiveRenderStatus, 0, &interactive);

  if (EnvTruthy(L"DLSSNR_PASSTHROUGH")) {
    Logf("render: passthrough (env) %dx%d", src.width, src.height);
    return finish(true);
  }
  if (scale[0] < 0.4 || scale[1] < 0.4) {
    Logf("render: skip mipmap %dx%d scale=%.3f,%.3f", src.width, src.height, scale[0], scale[1]);
    return finish(true);
  }
  if (aborted()) { Logf("render: abort before NR"); return finish(true); }

  EnterCriticalSection(&data->renderLock);
  const uint32_t request = data->nextRequest.fetch_add(1) + 1;
  std::wstring runtime = FindRuntimeDir();
  if (runtime.empty()) {
    LeaveCriticalSection(&data->renderLock);
    Logf("render: runtime NOT found; passthrough");
    return finish(true);
  }

  std::vector<uint8_t> packed, outPacked;
  pix::PackRgba8(src, packed, true);
  outPacked.resize(packed.size());
  nr_eval_desc desc{};
  desc.width = src.width; desc.height = src.height; desc.adapter = -1;
  desc.instance_id = data->instanceId; desc.request_id = request;
  desc.timeout_ms = interactive ? 4000u : 15000u;
  desc.runtime_dir = runtime.c_str(); desc.in_rgba = packed.data(); desc.out_rgba = outPacked.data();
  FillNrParams(data, desc);
  Logf("render: nr_eval %dx%d request=%u:%u intensity=%.2f style=%u detail=%.2f interactive=%d",
       src.width, src.height, desc.instance_id, request, desc.model.intensity,
       desc.model.style, desc.detail, interactive);
  nr_status status = engine_client::Eval(desc);
  if (aborted()) {
    engine_client::Cancel(desc.instance_id, request);
    status = NR_ERR_SUPERSEDED;
  }
  if (status == NR_OK) {
    pix::UnpackRgba8(dst, outPacked, true);
    int protect = 0, showMask = 0;
    double thr = 0.80, soft = 0.15, mix = 1.0;
    if (data->hlProtectParam) gParamHost->paramGetValue(data->hlProtectParam, &protect);
    if (data->hlShowMaskParam) gParamHost->paramGetValue(data->hlShowMaskParam, &showMask);
    if (data->hlThresholdParam) gParamHost->paramGetValue(data->hlThresholdParam, &thr);
    if (data->hlSoftnessParam) gParamHost->paramGetValue(data->hlSoftnessParam, &soft);
    if (data->hlMixParam) gParamHost->paramGetValue(data->hlMixParam, &mix);
    if (protect || showMask)
      pix::RestoreHighlights(src, dst, static_cast<float>(thr), static_cast<float>(soft),
                             static_cast<float>(mix), showMask != 0);
    Logf("render: nr_eval OK request=%u:%u", desc.instance_id, request);
  } else {
    Logf("render: nr_eval %s request=%u:%u; passthrough", engine_client::StatusName(status),
         desc.instance_id, request);
    pix::CopyPassthrough(src, dst);
  }
  LeaveCriticalSection(&data->renderLock);
  gEffectHost->clipReleaseImage(srcImg); gEffectHost->clipReleaseImage(dstImg);
  return kOfxStatOK;
}
