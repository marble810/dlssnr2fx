#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Dav2Estimator {
public:
  ~Dav2Estimator();
  void Reset();
  bool EstimateRgba8(const wchar_t* runtimeDir, const uint8_t* rgba, int width, int height,
                     int interval, float ema, int64_t frameId, std::vector<float>& depth);
  bool EstimateRgba32f(const wchar_t* runtimeDir, const float* rgba, int width, int height,
                       int interval, float ema, int64_t frameId, std::vector<float>& depth);

private:
  bool Ensure(const wchar_t* runtimeDir);
  bool Infer(const std::vector<float>& nchw, int iw, int ih, std::vector<float>& outHw);
  bool RunFrame(const std::vector<float>& rgb01, int width, int height, int interval, float ema,
                int64_t frameId, std::vector<float>& depth);

  struct Impl;
  Impl* impl_ = nullptr;
};
