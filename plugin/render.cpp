#include "plugin.h"

#include "engine_client.h"
#include "log.h"
#include "pixio.h"
#include "runtime.h"

#include "srw.h"

#include <algorithm>
#include <cmath>
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
  return pix::FitWorldToRowBytes(img);
}

size_t CommittedBytes(const uint8_t* p) {
  MEMORY_BASIC_INFORMATION mbi{};
  if (!p || !VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return 0;
  const auto* base = static_cast<const uint8_t*>(mbi.BaseAddress);
  const auto* regionEnd = base + mbi.RegionSize;
  if (p < base || p >= regionEnd) return 0;
  return static_cast<size_t>(regionEnd - p);
}

void ClampToCommitted(pix::PixWorld& img) {
  const size_t avail = CommittedBytes(img.data);
  if (!avail) return;
  pix::FitWorldToBytes(img, avail);
}

bool FinalizeImage(ImageInfo& img, const OfxRectI& win) {
  if (!pix::FitWorldToRowBytes(img)) return false;
  if (win.x2 > win.x1 && win.y2 > win.y1) {
    if (!pix::CropWorld(img, {win.x1, win.y1, win.x2, win.y2})) return false;
  }
  ClampToCommitted(img);
  return pix::WorldUsable(img);
}

int SehInvoke(void (*fn)(void*), void* ctx) {
  __try {
    fn(ctx);
    return 1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

struct PackCtx {
  ImageInfo* src;
  std::vector<uint8_t>* packed8;
  std::vector<float>* packedF;
  bool deep;
};
void PackFn(void* p) {
  auto* c = static_cast<PackCtx*>(p);
  if (c->deep) pix::PackRgba32f(*c->src, *c->packedF, true);
  else pix::PackRgba8(*c->src, *c->packed8, true);
}
struct CopyCtx {
  ImageInfo* src;
  ImageInfo* dst;
};
void CopyFn(void* p) {
  auto* c = static_cast<CopyCtx*>(p);
  pix::CopyPassthrough(*c->src, *c->dst);
}
}  // namespace

static bool GetD(OfxParamHandle h, double& v) {
  return h && gParamHost->paramGetValue(h, &v) == kOfxStatOK;
}
static bool GetI(OfxParamHandle h, int& v) {
  return h && gParamHost->paramGetValue(h, &v) == kOfxStatOK;
}

void FillNrParams(InstanceData* data, nr_eval_desc& desc) {
  double d = 1.0;
  int i = 0;
  if (GetD(data->intensityParam, d)) desc.model.intensity = static_cast<float>(d);
  if (GetI(data->styleParam, i)) desc.model.style = static_cast<uint32_t>(i);
  if (GetD(data->detailParam, d)) desc.detail = static_cast<float>(d);
  if (GetD(data->colorParam, d)) desc.color = static_cast<float>(d);
  if (GetI(data->uiParam, i)) desc.model.ui_correction = i != 0;
  if (GetD(data->localStructureParam, d)) desc.model.local_structure = static_cast<float>(d);
  if (GetD(data->localToneParam, d)) desc.model.local_tone = static_cast<float>(d);
  if (GetD(data->skinParam, d)) desc.model.skin = static_cast<float>(d);
  if (GetD(data->globalToneParam, d)) desc.model.global_tone = static_cast<float>(d);
  if (GetI(data->presetParam, i)) desc.model.preset = static_cast<uint32_t>(i);
  if (GetI(data->hdrParam, i)) desc.hdr = i != 0;
  if (GetI(data->autoMaskParam, i)) desc.model.auto_mask = i != 0;
  i = -1;
  if (GetI(data->adapterParam, i)) desc.adapter = i;
  i = 1;
  if (data->fgModeParam) gParamHost->paramGetValue(data->fgModeParam, &i);
  desc.fg_mode = (i <= 0) ? NR_FG_ALWAYS_RESET : NR_FG_LOCK;
  i = 1;
  if (data->fgCoalesceParam) gParamHost->paramGetValue(data->fgCoalesceParam, &i);
  desc.fg_coalesce = i != 0;
  i = 2;
  if (data->mvecSourceParam) gParamHost->paramGetValue(data->mvecSourceParam, &i);
  desc.mvec_source = static_cast<nr_guide_source>(i);
  i = 1;
  if (data->nvofQualityParam) gParamHost->paramGetValue(data->nvofQualityParam, &i);
  desc.nvof_quality = static_cast<nr_nvof_quality>(i);
  i = 1;
  if (data->nvofHintParam) gParamHost->paramGetValue(data->nvofHintParam, &i);
  desc.nvof_hint = i != 0;
  i = 2;
  if (data->depthSourceParam) gParamHost->paramGetValue(data->depthSourceParam, &i);
  desc.depth_source = static_cast<nr_guide_source>(i);
  i = 4;
  if (data->dav2IntervalParam) gParamHost->paramGetValue(data->dav2IntervalParam, &i);
  desc.dav2_interval = i;
  d = 0.70;
  if (data->dav2EmaParam) gParamHost->paramGetValue(data->dav2EmaParam, &d);
  desc.dav2_ema = static_cast<float>(d);
}

OfxStatus Render(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs,
                 OfxPropertySetHandle /*outArgs*/) {
  InstanceData* data = GetInstanceData(effect);
  if (!data || !data->sourceClip || !data->outputClip) return kOfxStatFailed;
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
  if (!FillImage(srcImg, src) || !FillImage(dstImg, dst) ||
      !FinalizeImage(src, renderWin) || !FinalizeImage(dst, renderWin)) {
    Logf("render: missing image data scale=%.3f,%.3f win=(%d,%d)-(%d,%d)",
         scale[0], scale[1], renderWin.x1, renderWin.y1, renderWin.x2, renderWin.y2);
    gEffectHost->clipReleaseImage(srcImg); gEffectHost->clipReleaseImage(dstImg);
    return kOfxStatFailed;
  }
  const int packedRow = src.width * src.nComp * src.bytesPerComp;
  if (src.width == dst.width && packedRow > 0 &&
      std::abs(dst.rowBytes) == packedRow &&
      std::abs(src.rowBytes) == 2 * packedRow) {
    Logf("render: src stride %d looks like full-res padding on %dx%d; using %d",
         src.rowBytes, src.width, src.height, dst.rowBytes);
    src.rowBytes = src.rowBytes < 0 ? -packedRow : packedRow;
  }
  Logf("render: begin src=%dx%d dst=%dx%d srcRow=%d dstRow=%d bpc=%d nComp=%d scale=%.3f,%.3f win=(%d,%d)-(%d,%d)",
       src.width, src.height, dst.width, dst.height, src.rowBytes, dst.rowBytes,
       src.bytesPerComp, src.nComp, scale[0], scale[1],
       renderWin.x1, renderWin.y1, renderWin.x2, renderWin.y2);
  const bool deepSrc = pix::PackAsFloat(src);
  const int64_t frameId = static_cast<int64_t>(std::llround(time));
  auto release = [&]() {
    gEffectHost->clipReleaseImage(srcImg);
    gEffectHost->clipReleaseImage(dstImg);
  };
  auto finishCopy = [&]() {
    CopyCtx ctx{&src, &dst};
    if (!SehInvoke(CopyFn, &ctx)) {
      Logf("render: fault copying %dx%d", src.width, src.height);
      release();
      return kOfxStatFailed;
    }
    release();
    return kOfxStatOK;
  };
  auto aborted = [&]() { return gEffectHost->abort && gEffectHost->abort(effect); };
  int interactive = 0;
  gPropHost->propGetInt(inArgs, kOfxImageEffectPropInteractiveRenderStatus, 0, &interactive);
  auto highlightParams = [&](double& thr, double& soft, double& mix, int& protect) {
    protect = 1;
    thr = 0.80; soft = 0.15; mix = 1.0;
    if (data->hlProtectParam) gParamHost->paramGetValue(data->hlProtectParam, &protect);
    if (data->hlThresholdParam) gParamHost->paramGetValue(data->hlThresholdParam, &thr);
    if (data->hlSoftnessParam) gParamHost->paramGetValue(data->hlSoftnessParam, &soft);
    if (data->hlMixParam) gParamHost->paramGetValue(data->hlMixParam, &mix);
  };
  int view = 0;
  if (data->viewParam) gParamHost->paramGetValue(data->viewParam, &view);
  if (view == kViewSource) {
    Logf("render: view source %dx%d", src.width, src.height);
    return finishCopy();
  }
  if (view == kViewHighlight) {
    int protect = 1;
    double thr = 0.80, soft = 0.15, mix = 1.0;
    highlightParams(thr, soft, mix, protect);
    CopyCtx ctx{&src, &dst};
    if (!SehInvoke(CopyFn, &ctx)) {
      Logf("render: fault copying highlight %dx%d", src.width, src.height);
      release();
      return kOfxStatFailed;
    }
    pix::RestoreHighlights(src, dst, static_cast<float>(thr), static_cast<float>(soft),
                           static_cast<float>(mix), true);
    release();
    return kOfxStatOK;
  }

  if (EnvTruthy(L"DLSSNR_PASSTHROUGH")) {
    Logf("render: passthrough (env) %dx%d", src.width, src.height);
    return finishCopy();
  }
  if (scale[0] < 0.4 || scale[1] < 0.4) {
    Logf("render: skip mipmap %dx%d scale=%.3f,%.3f", src.width, src.height, scale[0], scale[1]);
    return finishCopy();
  }
  if (src.width < 16 || src.height < 16) {
    Logf("render: skip tiny %dx%d", src.width, src.height);
    return finishCopy();
  }
  if (src.width != dst.width || src.height != dst.height) {
    Logf("render: skip size mismatch src=%dx%d dst=%dx%d", src.width, src.height,
         dst.width, dst.height);
    return finishCopy();
  }
  if (aborted()) {
    Logf("render: abort before NR frame=%lld skip", static_cast<long long>(frameId));
    release();
    return kOfxStatFailed;
  }

  CsLock renderGuard(data->renderLock);
  const uint32_t request = data->nextRequest.fetch_add(1) + 1;
  std::wstring runtime = FindRuntimeDir();
  if (runtime.empty()) {
    Logf("render: runtime NOT found; passthrough");
    return finishCopy();
  }

  std::vector<uint8_t> packed8, out8;
  std::vector<float> packedF, outF;
  const bool deep = deepSrc;
  PackCtx packCtx{&src, &packed8, &packedF, deep};
  if (!SehInvoke(PackFn, &packCtx)) {
    Logf("render: fault packing source %dx%d rowBytes=%d", src.width, src.height, src.rowBytes);
    release();
    return kOfxStatFailed;
  }
  if (deep) outF.resize(packedF.size());
  else out8.resize(packed8.size());
  nr_eval_desc desc{};
  desc.width = src.width; desc.height = src.height; desc.adapter = -1;
  desc.instance_id = data->instanceId; desc.request_id = request;
  desc.timeout_ms = interactive ? 20000u : 30000u;
  desc.runtime_dir = runtime.c_str();
  desc.format = deep ? NR_PIX_RGBA32F : NR_PIX_RGBA8;
  desc.in_rgba = deep ? static_cast<const void*>(packedF.data()) : packed8.data();
  desc.out_rgba = deep ? static_cast<void*>(outF.data()) : out8.data();
  FillNrParams(data, desc);
  desc.frame_id = frameId;
  desc.frame_step = 1;
  std::vector<uint8_t> prev8;
  std::vector<float> prevF;
  OfxPropertySetHandle prevImg = nullptr;
  if (gEffectHost->clipGetImage(data->sourceClip, time - 1.0, nullptr, &prevImg) == kOfxStatOK) {
    ImageInfo prev{};
    if (FillImage(prevImg, prev) && FinalizeImage(prev, renderWin) &&
        prev.width == src.width && prev.height == src.height &&
        prev.bytesPerComp == src.bytesPerComp) {
      if (deep) {
        pix::PackRgba32f(prev, prevF, true);
        desc.in_prev_rgba = prevF.data();
      } else {
        pix::PackRgba8(prev, prev8, true);
        desc.in_prev_rgba = prev8.data();
      }
    }
    gEffectHost->clipReleaseImage(prevImg);
  }
  std::vector<float> guideMvec, guideDepth;
  if (desc.mvec_source == NR_GUIDE_INPUT && data->mvecClip) {
    OfxPropertySetHandle mvecImg = nullptr;
    if (gEffectHost->clipGetImage(data->mvecClip, time, nullptr, &mvecImg) == kOfxStatOK) {
      ImageInfo mv{};
      if (FillImage(mvecImg, mv) && FinalizeImage(mv, renderWin) &&
          mv.width == src.width && mv.height == src.height) {
        int dir = 0, unit = 0, enc = 0;
        double range = 64.0;
        if (data->mvecDirParam) gParamHost->paramGetValue(data->mvecDirParam, &dir);
        if (data->mvecUnitParam) gParamHost->paramGetValue(data->mvecUnitParam, &unit);
        if (data->mvecEncParam) gParamHost->paramGetValue(data->mvecEncParam, &enc);
        if (data->mvecRangeParam) gParamHost->paramGetValue(data->mvecRangeParam, &range);
        pix::PackMvec(mv, guideMvec, true, dir != 0, unit != 0, enc == 0, static_cast<float>(range));
        desc.in_mvec = guideMvec.data();
      }
      gEffectHost->clipReleaseImage(mvecImg);
    }
  }
  if (desc.depth_source == NR_GUIDE_INPUT && data->depthClip) {
    OfxPropertySetHandle depthImg = nullptr;
    if (gEffectHost->clipGetImage(data->depthClip, time, nullptr, &depthImg) == kOfxStatOK) {
      ImageInfo dz{};
      if (FillImage(depthImg, dz) && FinalizeImage(dz, renderWin) &&
          dz.width == src.width && dz.height == src.height) {
        int conv = 0;
        if (data->depthConvParam) gParamHost->paramGetValue(data->depthConvParam, &conv);
        pix::PackDepth(dz, guideDepth, true, conv != 0);
        desc.in_depth = guideDepth.data();
      }
      gEffectHost->clipReleaseImage(depthImg);
    }
  }
  const size_t pixels = static_cast<size_t>(src.width) * src.height;
  std::vector<float> usedMvec(pixels * 2, 0.f), usedDepth(pixels, 0.f);
  desc.out_mvec = usedMvec.data();
  desc.out_depth = usedDepth.data();
  desc.maps_only = (view == kViewDepth || view == kViewMvec);
  Logf("render: nr_eval %dx%d request=%u:%u intensity=%.2f style=%u detail=%.2f interactive=%d depth=%s format=%s frame=%lld fg=%d coalesce=%d prev=%d mvec=%d z=%d view=%d",
       src.width, src.height, desc.instance_id, request, desc.model.intensity,
       desc.model.style, desc.detail, interactive, src.depth ? src.depth : "?",
       deep ? "RGBA32F" : "RGBA8", static_cast<long long>(desc.frame_id),
       static_cast<int>(desc.fg_mode), desc.fg_coalesce, desc.in_prev_rgba ? 1 : 0,
       desc.in_mvec ? 1 : 0, desc.in_depth ? 1 : 0, view);
  nr_status status = engine_client::Eval(desc);
  if (aborted()) engine_client::Cancel(desc.instance_id, request);
  if (status != NR_OK) {
    Logf("render: nr_eval %s request=%u:%u; skip", engine_client::StatusName(status),
         desc.instance_id, request);
    release();
    return kOfxStatFailed;
  }
  if (view == kViewDepth) {
    pix::VizDepth(dst, usedDepth.data(), true, pixels);
  } else if (view == kViewMvec) {
    pix::VizMvec(dst, usedMvec.data(), true, pixels);
  } else {
    CopyCtx copyCtx{&src, &dst};
    SehInvoke(CopyFn, &copyCtx);
    if (deep) pix::UnpackRgba32f(dst, outF, true);
    else pix::UnpackRgba8(dst, out8, true);
    int protect = 0;
    double thr = 0.80, soft = 0.15, mix = 1.0;
    highlightParams(thr, soft, mix, protect);
    if (protect)
      pix::RestoreHighlights(src, dst, static_cast<float>(thr), static_cast<float>(soft),
                             static_cast<float>(mix), false);
  }
  Logf("render: nr_eval OK request=%u:%u", desc.instance_id, request);
  release();
  return kOfxStatOK;
}
