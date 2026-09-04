#include "plugin.h"

#include <cstring>

OfxStatus Describe(OfxImageEffectHandle effect) {
  OfxPropertySetHandle props;
  gEffectHost->getPropertySet(effect, &props);

  gPropHost->propSetString(props, kOfxPropLabel, 0, kPluginLabel);
  gPropHost->propSetString(props, kOfxPropShortLabel, 0, "DLSSNR");
  gPropHost->propSetString(props, kOfxPropLongLabel, 0, kPluginLabel);
  gPropHost->propSetString(props, kOfxImageEffectPluginPropGrouping, 0, kPluginGrouping);

  gPropHost->propSetString(props, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);

  gPropHost->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthByte);
  gPropHost->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 1, kOfxBitDepthShort);
  gPropHost->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 2, kOfxBitDepthFloat);

  gPropHost->propSetInt(props, kOfxImageEffectPropSupportsTiles, 0, 0);
  gPropHost->propSetInt(props, kOfxImageEffectPropSupportsMultiResolution, 0, 0);
  gPropHost->propSetInt(props, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
  gPropHost->propSetInt(props, kOfxImageEffectPropTemporalClipAccess, 0, 0);
  gPropHost->propSetInt(props, kOfxImageEffectPluginPropFieldRenderTwiceAlways, 0, 0);

  return kOfxStatOK;
}

static void SetClipProps(OfxPropertySetHandle clipProps) {
  gPropHost->propSetString(clipProps, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
  gPropHost->propSetString(clipProps, kOfxImageClipPropFieldExtraction, 0, kOfxImageFieldBoth);
  gPropHost->propSetInt(clipProps, kOfxImageEffectPropSupportsTiles, 0, 0);
}

static void FinishParam(OfxPropertySetHandle p, const char* hint, const char* parent) {
  if (hint)
    gPropHost->propSetString(p, kOfxParamPropHint, 0, hint);
  if (parent)
    gPropHost->propSetString(p, kOfxParamPropParent, 0, parent);
  gPropHost->propSetInt(p, kOfxParamPropEvaluateOnChange, 0, 1);
}

static void DoubleRange(OfxPropertySetHandle p, double def, double mn, double mx) {
  gPropHost->propSetDouble(p, kOfxParamPropDefault, 0, def);
  gPropHost->propSetDouble(p, kOfxParamPropMin, 0, mn);
  gPropHost->propSetDouble(p, kOfxParamPropMax, 0, mx);
  gPropHost->propSetDouble(p, kOfxParamPropDisplayMin, 0, mn);
  gPropHost->propSetDouble(p, kOfxParamPropDisplayMax, 0, mx);
}

OfxStatus DescribeInContext(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs) {
  char* context = nullptr;
  gPropHost->propGetString(inArgs, kOfxImageEffectPropContext, 0, &context);
  if (!context || strcmp(context, kOfxImageEffectContextFilter) != 0)
    return kOfxStatErrUnsupported;

  OfxPropertySetHandle clipProps = nullptr;
  gEffectHost->clipDefine(effect, kOfxImageEffectSimpleSourceClipName, &clipProps);
  SetClipProps(clipProps);
  gEffectHost->clipDefine(effect, kOfxImageEffectOutputClipName, &clipProps);
  SetClipProps(clipProps);

  OfxParamSetHandle params;
  gEffectHost->getParamSet(effect, &params);
  OfxPropertySetHandle p = nullptr;

  gParamHost->paramDefine(params, kOfxParamTypeGroup, "grpLook", &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Look");

  gParamHost->paramDefine(params, kOfxParamTypeDouble, kParamIntensity, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Intensity");
  DoubleRange(p, 1.0, 0.0, 2.0);
  FinishParam(p, "DLSSNR.Intensity", "grpLook");

  gParamHost->paramDefine(params, kOfxParamTypeChoice, kParamStyle, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Style");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 0, "Default");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 1, "Natural");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 2, "Cinematic");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 0);
  FinishParam(p, "--nr-style 0/1/2", "grpLook");

  gParamHost->paramDefine(params, kOfxParamTypeDouble, kParamDetail, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Detail");
  DoubleRange(p, 1.0, 0.0, 1.0);
  FinishParam(p, "0 = original, 1 = full NR composite", "grpLook");

  gParamHost->paramDefine(params, kOfxParamTypeDouble, kParamColor, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Color");
  DoubleRange(p, 1.0, 0.0, 1.0);
  FinishParam(p, "0 = keep original hue, 1 = NR colour", "grpLook");

  gParamHost->paramDefine(params, kOfxParamTypeBoolean, kParamUiCorrection, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "UI Correction");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 0);
  FinishParam(p, "DLSSNR.UICorrection", "grpLook");

  gParamHost->paramDefine(params, kOfxParamTypeGroup, "grpStructure", &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Structure");

  gParamHost->paramDefine(params, kOfxParamTypeDouble, kParamLocalStructure, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Local Structure");
  DoubleRange(p, 1.0, 0.0, 2.0);
  FinishParam(p, "DLSSNR.LocalStructureStrength", "grpStructure");

  gParamHost->paramDefine(params, kOfxParamTypeDouble, kParamLocalTone, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Local Tone");
  DoubleRange(p, 1.0, 0.0, 2.0);
  FinishParam(p, "DLSSNR.LocalToneStrength", "grpStructure");

  gParamHost->paramDefine(params, kOfxParamTypeDouble, kParamSkin, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Skin");
  DoubleRange(p, -1.0, -1.0, 2.0);
  FinishParam(p, "SkinStructureStrength. -1 = model default", "grpStructure");

  gParamHost->paramDefine(params, kOfxParamTypeDouble, kParamGlobalTone, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Global Tone");
  DoubleRange(p, -1.0, -1.0, 2.0);
  FinishParam(p, "GlobalToneStrength. -1 = model default", "grpStructure");

  gParamHost->paramDefine(params, kOfxParamTypeGroup, "grpHighlight", &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Highlight Protect");

  gParamHost->paramDefine(params, kOfxParamTypeBoolean, kParamHlProtect, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Enable");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 1);
  FinishParam(p, "Restore original pixels in SDR highlights that NR crushes", "grpHighlight");

  gParamHost->paramDefine(params, kOfxParamTypeDouble, kParamHlThreshold, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Threshold");
  DoubleRange(p, 0.80, 0.0, 1.0);
  FinishParam(p, "Start restoring where max(R,G,B) exceeds this (8-bit SDR ~204)", "grpHighlight");

  gParamHost->paramDefine(params, kOfxParamTypeDouble, kParamHlSoftness, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Softness");
  DoubleRange(p, 0.15, 0.0, 1.0);
  FinishParam(p, "Ramp width above threshold", "grpHighlight");

  gParamHost->paramDefine(params, kOfxParamTypeDouble, kParamHlMix, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Mix");
  DoubleRange(p, 1.0, 0.0, 1.0);
  FinishParam(p, "How much original highlight to put back", "grpHighlight");

  gParamHost->paramDefine(params, kOfxParamTypeBoolean, kParamHlShowMask, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Show Mask");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 0);
  FinishParam(p, "Preview the highlight mask (white = protected)", "grpHighlight");

  gParamHost->paramDefine(params, kOfxParamTypeGroup, "grpMotion", &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Motion");

  gParamHost->paramDefine(params, kOfxParamTypeBoolean, kParamMotion, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Motion Vectors");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 0);
  FinishParam(p, "Optical-flow motion for temporal NR. Off for stills.", "grpMotion");

  gParamHost->paramDefine(params, kOfxParamTypeChoice, kParamMotionEngine, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Motion Engine");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 0, "Auto");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 1, "NVOF");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 2, "Lucas-Kanade");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 0);
  FinishParam(p, "auto / nvof (hardware) / lk", "grpMotion");

  gParamHost->paramDefine(params, kOfxParamTypeBoolean, kParamMotionVis, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Motion Visualize");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 0);
  FinishParam(p, "Output flow visualisation instead of NR (debug)", "grpMotion");

  gParamHost->paramDefine(params, kOfxParamTypeGroup, "grpAdvanced", &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Advanced");

  gParamHost->paramDefine(params, kOfxParamTypeInteger, kParamPreset, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "NR Preset");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 0);
  gPropHost->propSetInt(p, kOfxParamPropMin, 0, 0);
  gPropHost->propSetInt(p, kOfxParamPropMax, 0, 8);
  FinishParam(p, "DLSSNR render preset hint (--nr-preset)", "grpAdvanced");

  gParamHost->paramDefine(params, kOfxParamTypeBoolean, kParamHdr, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "HDR / Linear");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 0);
  FinishParam(p, "Feed linear colour instead of the sRGB proxy", "grpAdvanced");

  gParamHost->paramDefine(params, kOfxParamTypeBoolean, kParamAutoMask, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Auto Mask");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 0);
  FinishParam(p, "DLSSNR.UseAutoMask", "grpAdvanced");

  gParamHost->paramDefine(params, kOfxParamTypeInteger, kParamAdapter, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "GPU Adapter");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, -1);
  gPropHost->propSetInt(p, kOfxParamPropMin, 0, -1);
  gPropHost->propSetInt(p, kOfxParamPropMax, 0, 8);
  FinishParam(p, "DXGI adapter index. -1 = highest performance", "grpAdvanced");

  return kOfxStatOK;
}

OfxStatus GetClipPreferences(OfxImageEffectHandle /*effect*/, OfxPropertySetHandle /*inArgs*/,
                             OfxPropertySetHandle /*outArgs*/) {
  return kOfxStatReplyDefault;
}
