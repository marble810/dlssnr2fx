#include "dav2.h"

#include "../third_party/onnxruntime_c_api.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
constexpr int kLongSide = 336;
constexpr int kPatch = 14;

bool FileExists(const std::wstring& path) {
  const DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring FindModel(const wchar_t* runtimeDir) {
  wchar_t env[MAX_PATH]{};
  DWORD n = GetEnvironmentVariableW(L"DLSSNR_DAV2_MODEL", env, MAX_PATH);
  if (n > 0 && n < MAX_PATH && FileExists(env)) return env;
  if (!runtimeDir || !*runtimeDir) return {};
  const wchar_t* names[] = {L"\\dav2_vits.onnx", L"\\depth_anything_v2_vits.onnx",
                            L"\\depth_anything_v2_vits_fp16.onnx", L"\\model_fp16.onnx"};
  for (const wchar_t* name : names) {
    std::wstring path = std::wstring(runtimeDir) + name;
    if (FileExists(path)) return path;
  }
  return {};
}

void Rgba8ToRgb01(const uint8_t* rgba, int w, int h, std::vector<float>& rgb) {
  rgb.resize(static_cast<size_t>(w) * h * 3);
  for (int i = 0; i < w * h; ++i) {
    rgb[i * 3 + 0] = rgba[i * 4 + 0] / 255.f;
    rgb[i * 3 + 1] = rgba[i * 4 + 1] / 255.f;
    rgb[i * 3 + 2] = rgba[i * 4 + 2] / 255.f;
  }
}

void Rgba32fToRgb01(const float* rgba, int w, int h, std::vector<float>& rgb) {
  rgb.resize(static_cast<size_t>(w) * h * 3);
  for (int i = 0; i < w * h; ++i) {
    auto q = [](float v) {
      if (!std::isfinite(v)) v = 0.f;
      return std::max(0.f, std::min(1.f, v));
    };
    rgb[i * 3 + 0] = q(rgba[i * 4 + 0]);
    rgb[i * 3 + 1] = q(rgba[i * 4 + 1]);
    rgb[i * 3 + 2] = q(rgba[i * 4 + 2]);
  }
}

void LetterboxNchw(const std::vector<float>& rgb, int w, int h, int iw, int ih,
                   std::vector<float>& nchw) {
  nchw.assign(static_cast<size_t>(3) * iw * ih, 0.f);
  static const float mean[3] = {0.485f, 0.456f, 0.406f};
  static const float stdv[3] = {0.229f, 0.224f, 0.225f};
  for (int y = 0; y < ih; ++y) {
    const float sy = std::min(h - 1.f, (y + 0.5f) * h / ih - 0.5f);
    const int y0 = std::max(0, std::min(h - 1, (int)std::floor(sy)));
    const int y1 = std::max(0, std::min(h - 1, y0 + 1));
    const float ty = sy - y0;
    for (int x = 0; x < iw; ++x) {
      const float sx = std::min(w - 1.f, (x + 0.5f) * w / iw - 0.5f);
      const int x0 = std::max(0, std::min(w - 1, (int)std::floor(sx)));
      const int x1 = std::max(0, std::min(w - 1, x0 + 1));
      const float tx = sx - x0;
      for (int c = 0; c < 3; ++c) {
        auto at = [&](int xx, int yy) { return rgb[(static_cast<size_t>(yy) * w + xx) * 3 + c]; };
        float v = (at(x0, y0) * (1 - tx) + at(x1, y0) * tx) * (1 - ty) +
                  (at(x0, y1) * (1 - tx) + at(x1, y1) * tx) * ty;
        nchw[static_cast<size_t>(c) * iw * ih + y * iw + x] = (v - mean[c]) / stdv[c];
      }
    }
  }
}

void ResizeDepth(const float* src, int sw, int sh, int dw, int dh, std::vector<float>& dst) {
  dst.resize(static_cast<size_t>(dw) * dh);
  for (int y = 0; y < dh; ++y) {
    const float sy = (sh == 1) ? 0.f : (y + 0.5f) * sh / dh - 0.5f;
    const int y0 = std::max(0, std::min(sh - 1, (int)std::floor(sy)));
    const int y1 = std::max(0, std::min(sh - 1, y0 + 1));
    const float ty = sy - y0;
    for (int x = 0; x < dw; ++x) {
      const float sx = (sw == 1) ? 0.f : (x + 0.5f) * sw / dw - 0.5f;
      const int x0 = std::max(0, std::min(sw - 1, (int)std::floor(sx)));
      const int x1 = std::max(0, std::min(sw - 1, x0 + 1));
      const float tx = sx - x0;
      const float a = src[y0 * sw + x0];
      const float b = src[y0 * sw + x1];
      const float c = src[y1 * sw + x0];
      const float d = src[y1 * sw + x1];
      dst[static_cast<size_t>(y) * dw + x] =
          (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
    }
  }
}

void Percentile01(std::vector<float>& z) {
  if (z.empty()) return;
  std::vector<float> sorted = z;
  std::sort(sorted.begin(), sorted.end());
  const float p02 = sorted[static_cast<size_t>(sorted.size() * 0.02)];
  const float p98 = sorted[static_cast<size_t>(sorted.size() * 0.98)];
  const float range = std::max(1e-6f, p98 - p02);
  for (float& v : z) v = std::max(0.f, std::min(1.f, (v - p02) / range));
}
}  // namespace

struct Dav2Estimator::Impl {
  HMODULE module = nullptr;
  const OrtApi* api = nullptr;
  OrtEnv* env = nullptr;
  OrtSession* session = nullptr;
  OrtSessionOptions* options = nullptr;
  OrtMemoryInfo* mem = nullptr;
  std::wstring modelPath;
  std::string inputName;
  std::string outputName;
  std::vector<float> lastDepth;
  int lastW = 0;
  int lastH = 0;
  int64_t lastInferFrame = 0x7fffffffffffffffLL;
  bool ready = false;

  ~Impl() { Destroy(); }

  void Destroy() {
    if (!api) {
      if (module) FreeLibrary(module);
      module = nullptr;
      ready = false;
      return;
    }
    if (session) api->ReleaseSession(session);
    if (options) api->ReleaseSessionOptions(options);
    if (mem) api->ReleaseMemoryInfo(mem);
    if (env) api->ReleaseEnv(env);
    session = nullptr;
    options = nullptr;
    mem = nullptr;
    env = nullptr;
    api = nullptr;
    if (module) FreeLibrary(module);
    module = nullptr;
    ready = false;
    lastDepth.clear();
  }

  bool LoadOrt(const wchar_t* runtimeDir) {
    Destroy();
    std::wstring dll = runtimeDir ? std::wstring(runtimeDir) + L"\\onnxruntime.dll" : L"";
    if (!dll.empty()) module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) module = LoadLibraryW(L"onnxruntime.dll");
    if (!module) return false;
    auto getBase = reinterpret_cast<const OrtApiBase*(ORT_API_CALL*)(void)>(
        GetProcAddress(module, "OrtGetApiBase"));
    if (!getBase) return false;
    const OrtApiBase* base = getBase();
    if (!base || !base->GetApi) return false;
    api = base->GetApi(ORT_API_VERSION);
    if (!api) api = base->GetApi(16);
    if (!api) api = base->GetApi(14);
    if (!api) return false;
    if (api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "dlssnr-dav2", &env) != nullptr) return false;
    if (api->CreateSessionOptions(&options) != nullptr) return false;
    api->SetIntraOpNumThreads(options, 1);
    api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_BASIC);
    if (api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem) != nullptr)
      return false;
    return true;
  }

  bool LoadModel(const std::wstring& path) {
    if (!api || path.empty()) return false;
    if (api->CreateSession(env, path.c_str(), options, &session) != nullptr) return false;
    size_t inCount = 0, outCount = 0;
    api->SessionGetInputCount(session, &inCount);
    api->SessionGetOutputCount(session, &outCount);
    if (!inCount || !outCount) return false;
    OrtAllocator* alloc = nullptr;
    api->GetAllocatorWithDefaultOptions(&alloc);
    char* in = nullptr;
    char* out = nullptr;
    api->SessionGetInputName(session, 0, alloc, &in);
    api->SessionGetOutputName(session, 0, alloc, &out);
    if (!in || !out) return false;
    inputName = in;
    outputName = out;
    api->AllocatorFree(alloc, in);
    api->AllocatorFree(alloc, out);
    modelPath = path;
    ready = true;
    return true;
  }
};

Dav2Estimator::~Dav2Estimator() { delete impl_; }

void Dav2Estimator::Reset() {
  if (impl_) {
    impl_->lastDepth.clear();
    impl_->lastInferFrame = 0x7fffffffffffffffLL;
  }
}

bool Dav2Estimator::Ensure(const wchar_t* runtimeDir) {
  const std::wstring model = FindModel(runtimeDir);
  if (model.empty()) return false;
  if (!impl_) impl_ = new Impl();
  if (impl_->ready && impl_->modelPath == model) return true;
  if (!impl_->LoadOrt(runtimeDir)) return false;
  return impl_->LoadModel(model);
}

bool Dav2Estimator::Infer(const std::vector<float>& nchw, int iw, int ih, std::vector<float>& outHw) {
  if (!impl_ || !impl_->ready || !impl_->api) return false;
  const int64_t shape[4] = {1, 3, ih, iw};
  OrtValue* input = nullptr;
  if (impl_->api->CreateTensorWithDataAsOrtValue(impl_->mem, const_cast<float*>(nchw.data()),
                                                 nchw.size() * sizeof(float), shape, 4,
                                                 ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input) !=
      nullptr)
    return false;
  const char* inNames[] = {impl_->inputName.c_str()};
  const char* outNames[] = {impl_->outputName.c_str()};
  OrtValue* output = nullptr;
  const OrtStatus* st =
      impl_->api->Run(impl_->session, nullptr, inNames, &input, 1, outNames, 1, &output);
  impl_->api->ReleaseValue(input);
  if (st != nullptr || !output) {
    if (output) impl_->api->ReleaseValue(output);
    return false;
  }
  float* data = nullptr;
  impl_->api->GetTensorMutableData(output, reinterpret_cast<void**>(&data));
  OrtTensorTypeAndShapeInfo* info = nullptr;
  impl_->api->GetTensorTypeAndShape(output, &info);
  size_t dimCount = 0;
  impl_->api->GetDimensionsCount(info, &dimCount);
  std::vector<int64_t> dims(dimCount);
  impl_->api->GetDimensions(info, dims.data(), dimCount);
  impl_->api->ReleaseTensorTypeAndShapeInfo(info);
  int oh = ih, ow = iw;
  if (dimCount >= 2) {
    ow = static_cast<int>(dims[dimCount - 1]);
    oh = static_cast<int>(dims[dimCount - 2]);
  }
  if (!data || ow <= 0 || oh <= 0) {
    impl_->api->ReleaseValue(output);
    return false;
  }
  outHw.assign(data, data + static_cast<size_t>(ow) * oh);
  impl_->api->ReleaseValue(output);
  if (ow != iw || oh != ih) {
    std::vector<float> resized;
    ResizeDepth(outHw.data(), ow, oh, iw, ih, resized);
    outHw.swap(resized);
  }
  return true;
}

bool Dav2Estimator::RunFrame(const std::vector<float>& rgb01, int width, int height, int interval,
                             float ema, int64_t frameId, std::vector<float>& depth) {
  interval = std::max(1, interval);
  ema = std::max(0.f, std::min(0.95f, ema));
  const bool have = impl_->lastDepth.size() == static_cast<size_t>(width) * height &&
                    impl_->lastW == width && impl_->lastH == height;
  const bool due = !have || impl_->lastInferFrame == 0x7fffffffffffffffLL ||
                   (frameId - impl_->lastInferFrame >= interval) ||
                   (impl_->lastInferFrame - frameId >= interval);
  if (!due && have) {
    depth = impl_->lastDepth;
    return true;
  }
  int iw = width, ih = height;
  const int longEdge = std::max(width, height);
  if (longEdge != kLongSide) {
    const float s = kLongSide / static_cast<float>(longEdge);
    iw = std::max(kPatch, ((int)std::lround(width * s) / kPatch) * kPatch);
    ih = std::max(kPatch, ((int)std::lround(height * s) / kPatch) * kPatch);
  }
  std::vector<float> nchw;
  LetterboxNchw(rgb01, width, height, iw, ih, nchw);
  std::vector<float> inferHw;
  if (!Infer(nchw, iw, ih, inferHw)) return false;
  std::vector<float> full;
  ResizeDepth(inferHw.data(), iw, ih, width, height, full);
  Percentile01(full);
  if (have && ema > 0.f) {
    for (size_t i = 0; i < full.size(); ++i)
      full[i] = full[i] * (1.f - ema) + impl_->lastDepth[i] * ema;
  }
  impl_->lastDepth = full;
  impl_->lastW = width;
  impl_->lastH = height;
  impl_->lastInferFrame = frameId;
  depth.swap(full);
  return true;
}

bool Dav2Estimator::EstimateRgba8(const wchar_t* runtimeDir, const uint8_t* rgba, int width,
                                  int height, int interval, float ema, int64_t frameId,
                                  std::vector<float>& depth) {
  if (!rgba || !Ensure(runtimeDir)) {
    depth.clear();
    return false;
  }
  std::vector<float> rgb;
  Rgba8ToRgb01(rgba, width, height, rgb);
  return RunFrame(rgb, width, height, interval, ema, frameId, depth);
}

bool Dav2Estimator::EstimateRgba32f(const wchar_t* runtimeDir, const float* rgba, int width,
                                    int height, int interval, float ema, int64_t frameId,
                                    std::vector<float>& depth) {
  if (!rgba || !Ensure(runtimeDir)) {
    depth.clear();
    return false;
  }
  std::vector<float> rgb;
  Rgba32fToRgb01(rgba, width, height, rgb);
  return RunFrame(rgb, width, height, interval, ema, frameId, depth);
}
