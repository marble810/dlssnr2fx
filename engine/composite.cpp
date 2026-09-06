#include "composite.h"

#include <algorithm>
#include <cmath>

namespace {
float Clamp01(float value) { return std::max(0.0f, std::min(1.0f, value)); }
float ToLinear(float value) { return std::pow(Clamp01(value), 2.2f); }
float ToDisplay(float value) { return std::pow(Clamp01(value), 1.0f / 2.2f); }

void CompositePixel(const float original[3], const float neural[3], float output[3],
                    float amount, float chroma, bool hdr) {
  const float luma = neural[0] * 0.2126f + neural[1] * 0.7152f + neural[2] * 0.0722f;
  for (size_t c = 0; c < 3; ++c) {
    float o = original[c];
    float n = luma + (neural[c] - luma) * chroma;
    if (hdr) {
      o = ToLinear(o);
      n = ToLinear(n);
    }
    float value = o + (n - o) * amount;
    if (hdr) value = ToDisplay(value);
    output[c] = value;
  }
}
}  // namespace

void CompositeRgba32f(const float* original, const float* neural, float* output,
                      size_t pixels, float detail, float color, bool hdr) {
  const float amount = Clamp01(detail);
  const float chroma = Clamp01(color);
  for (size_t p = 0; p < pixels; ++p) {
    const size_t i = p * 4;
    CompositePixel(original + i, neural + i, output + i, amount, chroma, hdr);
    output[i + 3] = original[i + 3];
  }
}

void CompositeRgba8(const uint8_t* original, const uint8_t* neural, uint8_t* output,
                    size_t pixels, float detail, float color, bool hdr) {
  const float amount = Clamp01(detail);
  const float chroma = Clamp01(color);
  for (size_t p = 0; p < pixels; ++p) {
    const size_t i = p * 4;
    const float orig[3] = {original[i] / 255.0f, original[i + 1] / 255.0f, original[i + 2] / 255.0f};
    const float nr[3] = {neural[i] / 255.0f, neural[i + 1] / 255.0f, neural[i + 2] / 255.0f};
    float value[3];
    CompositePixel(orig, nr, value, amount, chroma, hdr);
    for (size_t c = 0; c < 3; ++c)
      output[i + c] = static_cast<uint8_t>(Clamp01(value[c]) * 255.0f + 0.5f);
    output[i + 3] = original[i + 3];
  }
}
