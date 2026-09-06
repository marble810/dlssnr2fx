#include "plugin.h"

#include "engine_client.h"
#include "log.h"
#include "pixio.h"
#include "runtime.h"

#include "srw.h"

#include <cmath>
#include <cstring>
#include <list>
#include <string>
#include <unordered_map>
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

struct HoldKey {
  int64_t frame = 0;
  int w = 0;
  int h = 0;
  bool deep = false;
  int view = 0;
  uint32_t style = 0;
  uint32_t preset = 0;
  int hdr = 0;
  int auto_mask = 0;
  int ui = 0;
  int fg_mode = 0;
  int mvec_source = 0;
  int depth_source = 0;
  int nvof_quality = 0;
  int nvof_hint = 0;
  int mvec_enc = 0;
  float mvec_range = 0;
  int dav2_interval = 0;
  float dav2_ema = 0;
  int hl_protect = 0;
  int hl_show = 0;
  float intensity = 0;
  float detail = 0;
  float color = 0;
  float local_structure = 0;
  float local_tone = 0;
  float skin = 0;
  float global_tone = 0;
  float hl_thr = 0;
  float hl_soft = 0;
  float hl_mix = 0;
};

bool SameLook(const HoldKey& a, const HoldKey& b) {
  return a.w == b.w && a.h == b.h && a.deep == b.deep && a.view == b.view && a.style == b.style && a.preset == b.preset &&
         a.hdr == b.hdr && a.auto_mask == b.auto_mask && a.ui == b.ui && a.fg_mode == b.fg_mode &&
         a.mvec_source == b.mvec_source && a.depth_source == b.depth_source &&
         a.nvof_quality == b.nvof_quality && a.nvof_hint == b.nvof_hint &&
         a.mvec_enc == b.mvec_enc && a.mvec_range == b.mvec_range &&
         a.dav2_interval == b.dav2_interval && a.dav2_ema == b.dav2_ema &&
         a.hl_protect == b.hl_protect && a.hl_show == b.hl_show && a.intensity == b.intensity &&
         a.detail == b.detail && a.color == b.color && a.local_structure == b.local_structure &&
         a.local_tone == b.local_tone && a.skin == b.skin && a.global_tone == b.global_tone &&
         a.hl_thr == b.hl_thr && a.hl_soft == b.hl_soft && a.hl_mix == b.hl_mix;
}

HoldKey MakeHoldKey(InstanceData* data, const nr_eval_desc& desc, bool deep) {
  HoldKey k;
  k.frame = desc.frame_id;
  k.w = desc.width;
  k.h = desc.height;
  k.deep = deep;
  if (data->viewParam) gParamHost->paramGetValue(data->viewParam, &k.view);
  k.style = desc.model.style;
  k.preset = desc.model.preset;
  k.hdr = desc.hdr;
  k.auto_mask = desc.model.auto_mask;
  k.ui = desc.model.ui_correction;
  k.fg_mode = static_cast<int>(desc.fg_mode);
  k.mvec_source = static_cast<int>(desc.mvec_source);
  k.depth_source = static_cast<int>(desc.depth_source);
  k.nvof_quality = static_cast<int>(desc.nvof_quality);
  k.nvof_hint = desc.nvof_hint;
  if (data->mvecEncParam) gParamHost->paramGetValue(data->mvecEncParam, &k.mvec_enc);
  {
    double range = 64.0;
    if (data->mvecRangeParam) gParamHost->paramGetValue(data->mvecRangeParam, &range);
    k.mvec_range = static_cast<float>(range);
  }
  k.dav2_interval = desc.dav2_interval;
  k.dav2_ema = desc.dav2_ema;
  k.intensity = desc.model.intensity;
  k.detail = desc.detail;
  k.color = desc.color;
  k.local_structure = desc.model.local_structure;
  k.local_tone = desc.model.local_tone;
  k.skin = desc.model.skin;
  k.global_tone = desc.model.global_tone;
  int i = 0;
  double d = 0;
  if (data->hlProtectParam) gParamHost->paramGetValue(data->hlProtectParam, &i);
  k.hl_protect = i;
  i = 0;
  if (data->hlShowMaskParam) gParamHost->paramGetValue(data->hlShowMaskParam, &i);
  k.hl_show = i;
  d = 0.80;
  if (data->hlThresholdParam) gParamHost->paramGetValue(data->hlThresholdParam, &d);
  k.hl_thr = static_cast<float>(d);
  d = 0.15;
  if (data->hlSoftnessParam) gParamHost->paramGetValue(data->hlSoftnessParam, &d);
  k.hl_soft = static_cast<float>(d);
  d = 1.0;
  if (data->hlMixParam) gParamHost->paramGetValue(data->hlMixParam, &d);
  k.hl_mix = static_cast<float>(d);
  return k;
}

struct SharedHold {
  SRWLOCK mu = SRWLOCK_INIT;
  struct Entry {
    HoldKey key;
    std::vector<uint8_t> bytes;
  };
  Entry last;
  std::unordered_map<int64_t, Entry> frames;
  std::list<int64_t> order;
  static constexpr size_t kMaxFrames = 48;

  bool Unpack(pix::PixWorld& dst, const HoldKey& key, bool exactFrame) {
    SrwExclusive guard(mu);
    const Entry* src = nullptr;
    auto it = frames.find(key.frame);
    if (it != frames.end() && SameLook(it->second.key, key)) src = &it->second;
    else if (!exactFrame && !last.bytes.empty() && SameLook(last.key, key)) src = &last;
    if (!src || src->bytes.empty()) return false;
    if (key.deep) {
      std::vector<float> tmp(src->bytes.size() / sizeof(float));
      if (tmp.empty()) return false;
      std::memcpy(tmp.data(), src->bytes.data(), tmp.size() * sizeof(float));
      pix::UnpackRgba32f(dst, tmp, true);
    } else {
      pix::UnpackRgba8(dst, src->bytes, true);
    }
    return true;
  }

  void Store(const HoldKey& key, const void* pixels, size_t bytes) {
    SrwExclusive guard(mu);
    Entry e;
    e.key = key;
    e.bytes.assign(static_cast<const uint8_t*>(pixels), static_cast<const uint8_t*>(pixels) + bytes);
    last = e;
    frames[key.frame] = e;
    order.remove(key.frame);
    order.push_back(key.frame);
    while (order.size() > kMaxFrames) {
      frames.erase(order.front());
      order.pop_front();
    }
  }
};
SharedHold gHold;

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
  if (!FillImage(srcImg, src) || !FillImage(dstImg, dst)) {
    Logf("render: missing image data");
    gEffectHost->clipReleaseImage(srcImg); gEffectHost->clipReleaseImage(dstImg);
    return kOfxStatFailed;
  }
  const bool deepSrc = pix::PackAsFloat(src);
  const int64_t frameId = static_cast<int64_t>(std::llround(time));
  nr_eval_desc look{};
  look.width = src.width;
  look.height = src.height;
  look.format = deepSrc ? NR_PIX_RGBA32F : NR_PIX_RGBA8;
  look.frame_id = frameId;
  FillNrParams(data, look);
  int view = 0;
  if (data->viewParam) gParamHost->paramGetValue(data->viewParam, &view);
  const HoldKey holdKey = MakeHoldKey(data, look, deepSrc);
  auto replayHold = [&](bool exact) -> bool { return gHold.Unpack(dst, holdKey, exact); };
  auto finish = [&](bool copy) {
    if (copy && !replayHold(false)) pix::CopyPassthrough(src, dst);
    gEffectHost->clipReleaseImage(srcImg); gEffectHost->clipReleaseImage(dstImg);
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
  if (view == kViewSource) {
    Logf("render: view source %dx%d", src.width, src.height);
    return finish(true);
  }
  if (view == kViewHighlight) {
    int protect = 1;
    double thr = 0.80, soft = 0.15, mix = 1.0;
    highlightParams(thr, soft, mix, protect);
    pix::CopyPassthrough(src, dst);
    pix::RestoreHighlights(src, dst, static_cast<float>(thr), static_cast<float>(soft),
                           static_cast<float>(mix), true);
    gEffectHost->clipReleaseImage(srcImg); gEffectHost->clipReleaseImage(dstImg);
    return kOfxStatOK;
  }

  if (EnvTruthy(L"DLSSNR_PASSTHROUGH")) {
    Logf("render: passthrough (env) %dx%d", src.width, src.height);
    return finish(true);
  }
  if (scale[0] < 0.4 || scale[1] < 0.4) {
    Logf("render: skip mipmap %dx%d scale=%.3f,%.3f", src.width, src.height, scale[0], scale[1]);
    return finish(true);
  }
  if (aborted()) {
    if (replayHold(true)) {
      Logf("render: abort before NR frame=%lld hold", static_cast<long long>(frameId));
      gEffectHost->clipReleaseImage(srcImg); gEffectHost->clipReleaseImage(dstImg);
      return kOfxStatOK;
    }
    Logf("render: abort before NR frame=%lld skip", static_cast<long long>(frameId));
    gEffectHost->clipReleaseImage(srcImg); gEffectHost->clipReleaseImage(dstImg);
    return kOfxStatFailed;
  }

  CsLock renderGuard(data->renderLock);
  const uint32_t request = data->nextRequest.fetch_add(1) + 1;
  std::wstring runtime = FindRuntimeDir();
  if (runtime.empty()) {
    Logf("render: runtime NOT found; passthrough");
    return finish(true);
  }

  if (gHold.Unpack(dst, holdKey, true)) {
    Logf("render: coalesce frame=%lld from shared hold", static_cast<long long>(frameId));
    gEffectHost->clipReleaseImage(srcImg); gEffectHost->clipReleaseImage(dstImg);
    return kOfxStatOK;
  }

  std::vector<uint8_t> packed8, out8;
  std::vector<float> packedF, outF;
  const bool deep = deepSrc;
  if (deep) {
    pix::PackRgba32f(src, packedF, true);
    outF.resize(packedF.size());
  } else {
    pix::PackRgba8(src, packed8, true);
    out8.resize(packed8.size());
  }
  nr_eval_desc desc{};
  desc.width = src.width; desc.height = src.height; desc.adapter = -1;
  desc.instance_id = data->instanceId; desc.request_id = request;
  desc.timeout_ms = interactive ? 20000u : 30000u;
  desc.runtime_dir = runtime.c_str();
  desc.format = deep ? NR_PIX_RGBA32F : NR_PIX_RGBA8;
  desc.in_rgba = deep ? static_cast<const void*>(packedF.data()) : packed8.data();
  desc.out_rgba = deep ? static_cast<void*>(outF.data()) : out8.data();
  FillNrParams(data, desc);
  desc.frame_id = static_cast<int64_t>(std::llround(time));
  desc.frame_step = 1;
  std::vector<uint8_t> prev8;
  std::vector<float> prevF;
  OfxPropertySetHandle prevImg = nullptr;
  if (gEffectHost->clipGetImage(data->sourceClip, time - 1.0, nullptr, &prevImg) == kOfxStatOK) {
    ImageInfo prev{};
    if (FillImage(prevImg, prev) && prev.width == src.width && prev.height == src.height &&
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
      if (FillImage(mvecImg, mv) && mv.width == src.width && mv.height == src.height) {
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
      if (FillImage(depthImg, dz) && dz.width == src.width && dz.height == src.height) {
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
  if (status == NR_OK) {
    if (view == kViewDepth) {
      pix::VizDepth(dst, usedDepth.data(), true);
    } else if (view == kViewMvec) {
      pix::VizMvec(dst, usedMvec.data(), true);
    } else {
      if (deep) pix::UnpackRgba32f(dst, outF, true);
      else pix::UnpackRgba8(dst, out8, true);
      int protect = 0;
      double thr = 0.80, soft = 0.15, mix = 1.0;
      highlightParams(thr, soft, mix, protect);
      if (protect)
        pix::RestoreHighlights(src, dst, static_cast<float>(thr), static_cast<float>(soft),
                               static_cast<float>(mix), false);
    }
    if (deep)
      gHold.Store(holdKey, outF.data(), outF.size() * sizeof(float));
    else
      gHold.Store(holdKey, out8.data(), out8.size());
    Logf("render: nr_eval OK request=%u:%u", desc.instance_id, request);
  } else {
    if (replayHold(true)) {
      Logf("render: nr_eval %s request=%u:%u; hold", engine_client::StatusName(status),
           desc.instance_id, request);
    } else {
      Logf("render: nr_eval %s request=%u:%u; skip", engine_client::StatusName(status),
           desc.instance_id, request);
      gEffectHost->clipReleaseImage(srcImg); gEffectHost->clipReleaseImage(dstImg);
      return kOfxStatFailed;
    }
  }
  gEffectHost->clipReleaseImage(srcImg); gEffectHost->clipReleaseImage(dstImg);
  return kOfxStatOK;
}
