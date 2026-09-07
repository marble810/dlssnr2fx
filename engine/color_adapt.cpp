#include "color_adapt.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
float Clamp01(float value) { return std::max(0.0f, std::min(1.0f, value)); }

float ProxyChannel(float value) {
  if (!std::isfinite(value)) return 0.0f;
  return Clamp01(value);
}

float FiniteOr(float value, float fallback) {
  return std::isfinite(value) ? value : fallback;
}
}  // namespace

void PrepareModelRgba32f(const float* src, float* proxy, size_t pixels) {
  if (!src || !proxy || pixels == 0) return;
  for (size_t i = 0; i < pixels * 4; ++i) proxy[i] = ProxyChannel(src[i]);
}

void ReconstructRgba32f(const float* original, const float* proxy, const float* neural,
                        float* output, size_t pixels, float detail, float color) {
  if (!original || !output || pixels == 0) return;
  const float amount = Clamp01(detail);
  if (amount == 0.0f) {
    if (output != original) std::memcpy(output, original, pixels * 4 * sizeof(float));
    return;
  }
  if (!proxy || !neural) {
    if (output != original) std::memcpy(output, original, pixels * 4 * sizeof(float));
    return;
  }
  const float chroma = Clamp01(color);
  for (size_t p = 0; p < pixels; ++p) {
    const size_t i = p * 4;
    const float n0 = FiniteOr(neural[i], proxy[i]);
    const float n1 = FiniteOr(neural[i + 1], proxy[i + 1]);
    const float n2 = FiniteOr(neural[i + 2], proxy[i + 2]);
    const float luma = n0 * 0.2126f + n1 * 0.7152f + n2 * 0.0722f;
    const float cn[3] = {luma + (n0 - luma) * chroma, luma + (n1 - luma) * chroma,
                         luma + (n2 - luma) * chroma};
    for (size_t c = 0; c < 3; ++c)
      output[i + c] = original[i + c] + (cn[c] - proxy[i + c]) * amount;
    output[i + 3] = original[i + 3];
  }
}
