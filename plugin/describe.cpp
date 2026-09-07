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
  gPropHost->propSetInt(props, kOfxImageEffectPropTemporalClipAccess, 0, 1);
  gPropHost->propSetInt(props, kOfxImageEffectPluginPropFieldRenderTwiceAlways, 0, 0);
  // Nuke renders OFX on worker threads. The in-process NGX session is process-global.
  gPropHost->propSetString(props, kOfxImageEffectPluginRenderThreadSafety, 0,
                           kOfxImageEffectRenderUnsafe);
  gPropHost->propSetInt(props, kOfxImageEffectPluginPropHostFrameThreading, 0, 0);

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
  gEffectHost->clipDefine(effect, kClipMVec, &clipProps);
  SetClipProps(clipProps);
  gPropHost->propSetInt(clipProps, kOfxImageClipPropOptional, 0, 1);
  gEffectHost->clipDefine(effect, kClipDepth, &clipProps);
  SetClipProps(clipProps);
  gPropHost->propSetInt(clipProps, kOfxImageClipPropOptional, 0, 1);

  OfxParamSetHandle params;
  gEffectHost->getParamSet(effect, &params);
  OfxPropertySetHandle p = nullptr;

  gParamHost->paramDefine(params, kOfxParamTypeChoice, kParamView, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "View");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 0, "Final Image");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 1, "Source Image");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 2, "Depth Map");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 3, "Motion Vector Map");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 4, "Highlight Mask");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 0);
  FinishParam(p, "Preview final NR, the source, or the depth / motion / highlight maps actually used.", nullptr);

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
  gPropHost->propSetInt(p, kOfxParamPropSecret, 0, 1);
  FinishParam(p, "Obsolete; use View = Highlight Mask", "grpHighlight");

  gParamHost->paramDefine(params, kOfxParamTypeGroup, "grpMVec", &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Motion Vectors");

  gParamHost->paramDefine(params, kOfxParamTypeChoice, kParamMvecSource, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Source");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 0, "Off");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 1, "Input");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 2, "Estimate (NVOF)");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 2);
  FinishParam(p, "Off = zeros. Input = MVec clip (RG). Estimate = NVIDIA Optical Flow, block-match fallback.",
              "grpMVec");

  gParamHost->paramDefine(params, kOfxParamTypeChoice, kParamMvecDir, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Direction");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 0, "Current to Previous");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 1, "Previous to Current");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 0);
  FinishParam(p, "NR wants current-to-previous. Invert if the render pass is the other way.",
              "grpMVec");

  gParamHost->paramDefine(params, kOfxParamTypeChoice, kParamMvecUnit, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Units");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 0, "Pixels");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 1, "UV");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 0);
  FinishParam(p, "After Centered encoding: Pixels uses Range; UV uses ±half frame.", "grpMVec");

  gParamHost->paramDefine(params, kOfxParamTypeChoice, kParamMvecEnc, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Encoding");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 0, "Centered 0.5 (AE)");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 1, "Signed 0=still");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 0);
  FinishParam(p, "AE cannot author negatives: 0.5=still, 0/1=±Range. Signed is EXR dx/dy in pixels.",
              "grpMVec");

  gParamHost->paramDefine(params, kOfxParamTypeDouble, kParamMvecRange, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Range (px)");
  DoubleRange(p, 64.0, 1.0, 1024.0);
  FinishParam(p, "Centered encoding: 0.5→1.0 maps to +Range pixels. 8-bit stays in 0-1.", "grpMVec");

  gParamHost->paramDefine(params, kOfxParamTypeChoice, kParamNvofQuality, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "NVOF Quality");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 0, "Slow");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 1, "Medium");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 2, "Fast");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 1);
  FinishParam(p, "NVIDIA Optical Flow preset. Slow = best, Fast = cheapest.", "grpMVec");

  gParamHost->paramDefine(params, kOfxParamTypeBoolean, kParamNvofHint, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "NVOF Temporal Hint");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 1);
  FinishParam(p, "Use previous flow as a hint. Turn off across cuts.", "grpMVec");

  gParamHost->paramDefine(params, kOfxParamTypeGroup, "grpDepth", &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Depth");

  gParamHost->paramDefine(params, kOfxParamTypeChoice, kParamDepthSource, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Source");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 0, "Off");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 1, "Input");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 2, "Estimate (DAV2)");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 2);
  FinishParam(p, "Off = zeros. Input = Depth clip (R). Estimate = Depth Anything V2 if onnxruntime + model are present.",
              "grpDepth");

  gParamHost->paramDefine(params, kOfxParamTypeChoice, kParamDepthConv, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Convention");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 0, "Inverse (near=1)");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 1, "Linear (near=0)");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 0);
  FinishParam(p, "NR inverted depth is near=1. Linear Z is converted.", "grpDepth");

  gParamHost->paramDefine(params, kOfxParamTypeInteger, kParamDav2Interval, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "DAV2 Interval");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 4);
  gPropHost->propSetInt(p, kOfxParamPropMin, 0, 1);
  gPropHost->propSetInt(p, kOfxParamPropMax, 0, 8);
  FinishParam(p, "Run Depth Anything every N frames; hold otherwise.", "grpDepth");

  gParamHost->paramDefine(params, kOfxParamTypeDouble, kParamDav2Ema, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "DAV2 Temporal Blend");
  DoubleRange(p, 0.70, 0.0, 0.95);
  FinishParam(p, "EMA toward previous depth. 0 = raw inference.", "grpDepth");

  gParamHost->paramDefine(params, kOfxParamTypeGroup, "grpFrameGuidance", &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Frame Guidance");

  gParamHost->paramDefine(params, kOfxParamTypeChoice, kParamFgMode, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Mode");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 0, "Always Reset");
  gPropHost->propSetString(p, kOfxParamPropChoiceOption, 1, "Lock History");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 1);
  FinishParam(p,
              "Always Reset: no temporal history. Lock History: keep history across frames.",
              "grpFrameGuidance");

  gParamHost->paramDefine(params, kOfxParamTypeBoolean, kParamFgCoalesce, &p);
  gPropHost->propSetString(p, kOfxPropLabel, 0, "Coalesce Same Frame");
  gPropHost->propSetInt(p, kOfxParamPropDefault, 0, 1);
  FinishParam(p, "Skip NGX when the same instance, inputs, and parameters render the same frame again",
              "grpFrameGuidance");

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
  FinishParam(p,
              "Kept for 8-bit gamma blend compatibility. Float HDR uses a bounded model proxy plus residual reconstruction and is not clipped to [0,1].",
              "grpAdvanced");

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
