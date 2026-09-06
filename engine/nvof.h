#pragma once

#include <d3d12.h>
#include <dxgi.h>

#include <cstdint>
#include <vector>

class NvofEstimator {
public:
  ~NvofEstimator();
  void Reset();
  bool EstimateRgba8(LUID adapter, const uint8_t* current, const uint8_t* previous, int width,
                     int height, int quality, bool temporalHint, std::vector<float>& xy);
  bool EstimateRgba32f(LUID adapter, const float* current, const float* previous, int width,
                       int height, int quality, bool temporalHint, std::vector<float>& xy);

private:
  bool Ensure(LUID adapter, int width, int height, int quality);
  bool Execute(const uint8_t* currentBgra, const uint8_t* previousBgra, bool temporalHint,
               std::vector<float>& xy);

  struct Impl;
  Impl* impl_ = nullptr;
};
