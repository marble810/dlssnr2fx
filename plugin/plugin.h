#pragma once

#include <windows.h>

#include <atomic>
#include <string>

#include "ofxImageEffect.h"
#include "ofxMultiThread.h"
#include "ofxParam.h"
#include "ofxProperty.h"

#include "../engine/dlssnr2fx.h"

#define kPluginIdentifier "com.dlss5ofx.DLSSNeural"
#define kPluginLabel "DLSS Neural Rendering"
#define kPluginGrouping "Filter"

#define kParamIntensity "intensity"
#define kParamStyle "style"
#define kParamDetail "detail"
#define kParamColor "color"
#define kParamUiCorrection "ui_correction"
#define kParamLocalStructure "local_structure"
#define kParamLocalTone "local_tone"
#define kParamSkin "skin"
#define kParamGlobalTone "global_tone"
#define kParamPreset "preset"
#define kParamHdr "hdr"
#define kParamAutoMask "auto_mask"
#define kParamAdapter "adapter"
#define kParamMotion "motion"
#define kParamMotionEngine "motion_engine"
#define kParamMotionVis "motion_vis"
#define kParamHlProtect "hl_protect"
#define kParamHlThreshold "hl_threshold"
#define kParamHlSoftness "hl_softness"
#define kParamHlMix "hl_mix"
#define kParamHlShowMask "hl_show_mask"

struct InstanceData {
  OfxImageClipHandle sourceClip = nullptr;
  OfxImageClipHandle outputClip = nullptr;
  OfxParamHandle intensityParam = nullptr;
  OfxParamHandle styleParam = nullptr;
  OfxParamHandle detailParam = nullptr;
  OfxParamHandle colorParam = nullptr;
  OfxParamHandle uiParam = nullptr;
  OfxParamHandle localStructureParam = nullptr;
  OfxParamHandle localToneParam = nullptr;
  OfxParamHandle skinParam = nullptr;
  OfxParamHandle globalToneParam = nullptr;
  OfxParamHandle presetParam = nullptr;
  OfxParamHandle hdrParam = nullptr;
  OfxParamHandle autoMaskParam = nullptr;
  OfxParamHandle adapterParam = nullptr;
  OfxParamHandle motionParam = nullptr;
  OfxParamHandle motionEngineParam = nullptr;
  OfxParamHandle motionVisParam = nullptr;
  OfxParamHandle hlProtectParam = nullptr;
  OfxParamHandle hlThresholdParam = nullptr;
  OfxParamHandle hlSoftnessParam = nullptr;
  OfxParamHandle hlMixParam = nullptr;
  OfxParamHandle hlShowMaskParam = nullptr;
  uint32_t instanceId = 0;
  std::atomic<uint32_t> nextRequest{0};
  CRITICAL_SECTION renderLock{};
  bool lockInit = false;
};

extern OfxHost* gHost;
extern OfxImageEffectSuiteV1* gEffectHost;
extern OfxPropertySuiteV1* gPropHost;
extern OfxParameterSuiteV1* gParamHost;
extern OfxMultiThreadSuiteV1* gThreadHost;

OfxStatus Describe(OfxImageEffectHandle effect);
OfxStatus DescribeInContext(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs);
OfxStatus Render(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs);
OfxStatus GetClipPreferences(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs,
                             OfxPropertySetHandle outArgs);

InstanceData* GetInstanceData(OfxImageEffectHandle effect);
std::wstring FindRuntimeDir();
bool EnvTruthy(const wchar_t* name);
void FillNrParams(InstanceData* data, nr_eval_desc& desc);
