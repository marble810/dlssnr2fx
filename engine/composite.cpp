#include "composite.h"

#include <algorithm>
#include <cmath>

namespace {
float Clamp01(float value) { return std::max(0.0f, std::min(1.0f, value)); }
float ToLinear(float value) { return std::pow(Clamp01(value), 2.2f); }
float ToDisplay(float value) { return std::pow(Clamp01(value), 1.0f / 2.2f); }
}

void CompositeRgba8(const uint8_t* original, const uint8_t* neural, uint8_t* output,
                    size_t pixels, float detail, float color, bool hdr) {
  const float amount = Clamp01(detail);
  const float chroma = Clamp01(color);
  for (size_t p = 0; p < pixels; ++p) {
    const size_t i = p * 4;
    float nr[3] = {neural[i] / 255.0f, neural[i + 1] / 255.0f, neural[i + 2] / 255.0f};
    const float luma = nr[0] * 0.2126f + nr[1] * 0.7152f + nr[2] * 0.0722f;
    for (size_t c = 0; c < 3; ++c) {
      float o = original[i + c] / 255.0f;
      float n = luma + (nr[c] - luma) * chroma;
      if (hdr) {
        o = ToLinear(o);
        n = ToLinear(n);
      }
      float value = o + (n - o) * amount;
      if (hdr) value = ToDisplay(value);
      output[i + c] = static_cast<uint8_t>(Clamp01(value) * 255.0f + 0.5f);
    }
    output[i + 3] = original[i + 3];
  }
}
