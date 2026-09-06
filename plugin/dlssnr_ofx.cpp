#include "plugin.h"

#include "engine_client.h"
#include "log.h"

#include <atomic>
#include <cstring>
#include <new>

OfxHost* gHost = nullptr;
OfxImageEffectSuiteV1* gEffectHost = nullptr;
OfxPropertySuiteV1* gPropHost = nullptr;
OfxParameterSuiteV1* gParamHost = nullptr;
OfxMultiThreadSuiteV1* gThreadHost = nullptr;
static std::atomic<uint32_t> gNextInstanceId{0};

InstanceData* GetInstanceData(OfxImageEffectHandle effect) {
  OfxPropertySetHandle props = nullptr;
  gEffectHost->getPropertySet(effect, &props);
  InstanceData* data = nullptr;
  gPropHost->propGetPointer(props, kOfxPropInstanceData, 0, (void**)&data);
  return data;
}

static void SetHost(OfxHost* host) {
  gHost = host;
}

static OfxStatus OnLoad() {
  LogInit();
  Logf("load");
  if (!gHost)
    return kOfxStatErrMissingHostFeature;
  gEffectHost = (OfxImageEffectSuiteV1*)gHost->fetchSuite(gHost->host, kOfxImageEffectSuite, 1);
  gPropHost = (OfxPropertySuiteV1*)gHost->fetchSuite(gHost->host, kOfxPropertySuite, 1);
  gParamHost = (OfxParameterSuiteV1*)gHost->fetchSuite(gHost->host, kOfxParameterSuite, 1);
  gThreadHost = (OfxMultiThreadSuiteV1*)gHost->fetchSuite(gHost->host, kOfxMultiThreadSuite, 1);
  if (!gEffectHost || !gPropHost || !gParamHost)
    return kOfxStatErrMissingHostFeature;
  return kOfxStatOK;
}

static OfxStatus OnUnload() {
  Logf("unload: shutting down in-process engine");
  engine_client::Shutdown();
  return kOfxStatOK;
}

static OfxStatus CreateInstance(OfxImageEffectHandle effect) {
  OfxPropertySetHandle props = nullptr;
  gEffectHost->getPropertySet(effect, &props);
  OfxParamSetHandle paramSet = nullptr;
  gEffectHost->getParamSet(effect, &paramSet);

  auto* data = new (std::nothrow) InstanceData();
  if (!data)
    return kOfxStatErrMemory;
  InitializeCriticalSection(&data->renderLock);
  data->lockInit = true;
  data->instanceId = gNextInstanceId.fetch_add(1) + 1;

  if (gEffectHost->clipGetHandle(effect, kOfxImageEffectSimpleSourceClipName, &data->sourceClip,
                                 nullptr) != kOfxStatOK)
    data->sourceClip = nullptr;
  if (gEffectHost->clipGetHandle(effect, kOfxImageEffectOutputClipName, &data->outputClip,
                                 nullptr) != kOfxStatOK)
    data->outputClip = nullptr;
  if (gEffectHost->clipGetHandle(effect, kClipMVec, &data->mvecClip, nullptr) != kOfxStatOK)
    data->mvecClip = nullptr;
  if (gEffectHost->clipGetHandle(effect, kClipDepth, &data->depthClip, nullptr) != kOfxStatOK)
    data->depthClip = nullptr;
  gPropHost->propSetInt(props, kOfxImageEffectInstancePropSequentialRender, 0, 2);
  gParamHost->paramGetHandle(paramSet, kParamView, &data->viewParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamIntensity, &data->intensityParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamStyle, &data->styleParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamDetail, &data->detailParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamColor, &data->colorParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamUiCorrection, &data->uiParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamLocalStructure, &data->localStructureParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamLocalTone, &data->localToneParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamSkin, &data->skinParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamGlobalTone, &data->globalToneParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamPreset, &data->presetParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamHdr, &data->hdrParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamAutoMask, &data->autoMaskParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamAdapter, &data->adapterParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamHlProtect, &data->hlProtectParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamHlThreshold, &data->hlThresholdParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamHlSoftness, &data->hlSoftnessParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamHlMix, &data->hlMixParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamHlShowMask, &data->hlShowMaskParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamFgMode, &data->fgModeParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamFgCoalesce, &data->fgCoalesceParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamMvecSource, &data->mvecSourceParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamMvecDir, &data->mvecDirParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamMvecUnit, &data->mvecUnitParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamMvecEnc, &data->mvecEncParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamMvecRange, &data->mvecRangeParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamNvofQuality, &data->nvofQualityParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamNvofHint, &data->nvofHintParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamDepthSource, &data->depthSourceParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamDepthConv, &data->depthConvParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamDav2Interval, &data->dav2IntervalParam, nullptr);
  gParamHost->paramGetHandle(paramSet, kParamDav2Ema, &data->dav2EmaParam, nullptr);

  gPropHost->propSetPointer(props, kOfxPropInstanceData, 0, data);
  return kOfxStatOK;
}

static OfxStatus DestroyInstance(OfxImageEffectHandle effect) {
  InstanceData* data = GetInstanceData(effect);
  if (data) {
    engine_client::Cancel(data->instanceId, data->nextRequest.load());
    if (data->lockInit)
      DeleteCriticalSection(&data->renderLock);
    OfxPropertySetHandle props = nullptr;
    gEffectHost->getPropertySet(effect, &props);
    gPropHost->propSetPointer(props, kOfxPropInstanceData, 0, nullptr);
    delete data;
  }
  return kOfxStatOK;
}

static OfxStatus MainEntry(const char* action, const void* handle, OfxPropertySetHandle inArgs,
                           OfxPropertySetHandle outArgs) {
  auto effect = (OfxImageEffectHandle)handle;
  try {
    if (strcmp(action, kOfxActionLoad) == 0)
      return OnLoad();
    if (strcmp(action, kOfxActionUnload) == 0)
      return OnUnload();
    if (strcmp(action, kOfxActionDescribe) == 0)
      return Describe(effect);
    if (strcmp(action, kOfxImageEffectActionDescribeInContext) == 0)
      return DescribeInContext(effect, inArgs);
    if (strcmp(action, kOfxActionCreateInstance) == 0)
      return CreateInstance(effect);
    if (strcmp(action, kOfxActionDestroyInstance) == 0)
      return DestroyInstance(effect);
    if (strcmp(action, kOfxImageEffectActionRender) == 0)
      return Render(effect, inArgs, outArgs);
    if (strcmp(action, kOfxImageEffectActionGetClipPreferences) == 0)
      return GetClipPreferences(effect, inArgs, outArgs);
    if (strcmp(action, kOfxImageEffectActionBeginSequenceRender) == 0)
      return kOfxStatOK;
    if (strcmp(action, kOfxImageEffectActionEndSequenceRender) == 0)
      return kOfxStatOK;
    return kOfxStatReplyDefault;
  } catch (...) {
    Logf("exception in action %s", action);
    return kOfxStatFailed;
  }
}

static OfxPlugin gPlugin = {kOfxImageEffectPluginApi,
                            kOfxImageEffectPluginApiVersion,
                            kPluginIdentifier,
                            1,
                            0,
                            SetHost,
                            MainEntry};

extern "C" {

int OfxGetNumberOfPlugins(void) {
  return 1;
}

OfxPlugin* OfxGetPlugin(int nth) {
  if (nth == 0)
    return &gPlugin;
  return nullptr;
}

}
