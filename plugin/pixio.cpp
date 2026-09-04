#include "pixio.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pix {

namespace {

inline uint8_t* PixelPtr(const PixWorld& img, int x, int y, int c) {
  return img.data + (y - img.bounds.y1) * img.rowBytes +
         (x - img.bounds.x1) * img.nComp * img.bytesPerComp +
         img.channelIndex[c] * img.bytesPerComp;
}

}  // namespace

uint8_t SampleByte(const PixWorld& img, int x, int y, int c) {
  if (c >= img.nComp)
    return 255;
  const uint8_t* p = PixelPtr(img, x, y, c);
  if (img.bytesPerComp == 1)
    return *p;
  if (img.bytesPerComp == 2) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    const uint32_t maxValue = img.maxChannelValue ? img.maxChannelValue : 65535;
    return (uint8_t)std::lround(std::min<uint32_t>(v, maxValue) * 255.0 / maxValue);
  }
  float f;
  std::memcpy(&f, p, 4);
  int v = (int)std::lround(std::max(0.f, std::min(1.f, f)) * 255.f);
  return (uint8_t)v;
}

void StoreByte(PixWorld& img, int x, int y, int c, uint8_t b) {
  if (c >= img.nComp)
    return;
  uint8_t* p = PixelPtr(img, x, y, c);
  if (img.bytesPerComp == 1) {
    *p = b;
  } else if (img.bytesPerComp == 2) {
    const uint32_t maxValue = img.maxChannelValue ? img.maxChannelValue : 65535;
    uint16_t v = (uint16_t)std::lround(b * maxValue / 255.0);
    std::memcpy(p, &v, 2);
  } else {
    float f = b / 255.f;
    std::memcpy(p, &f, 4);
  }
}

void PackRgba8(const PixWorld& img, std::vector<uint8_t>& out, bool flipVert) {
  out.resize((size_t)img.width * img.height * 4);
  size_t i = 0;
  const int yStart = flipVert ? img.bounds.y2 - 1 : img.bounds.y1;
  const int yEnd = flipVert ? img.bounds.y1 - 1 : img.bounds.y2;
  const int yStep = flipVert ? -1 : 1;
  for (int y = yStart; y != yEnd; y += yStep) {
    for (int x = img.bounds.x1; x < img.bounds.x2; ++x) {
      out[i++] = SampleByte(img, x, y, 0);
      out[i++] = SampleByte(img, x, y, 1);
      out[i++] = SampleByte(img, x, y, 2);
      out[i++] = SampleByte(img, x, y, 3);
    }
  }
}

void UnpackRgba8(PixWorld& img, const std::vector<uint8_t>& in, bool flipVert) {
  size_t i = 0;
  const int yStart = flipVert ? img.bounds.y2 - 1 : img.bounds.y1;
  const int yEnd = flipVert ? img.bounds.y1 - 1 : img.bounds.y2;
  const int yStep = flipVert ? -1 : 1;
  for (int y = yStart; y != yEnd; y += yStep) {
    for (int x = img.bounds.x1; x < img.bounds.x2; ++x) {
      StoreByte(img, x, y, 0, in[i++]);
      StoreByte(img, x, y, 1, in[i++]);
      StoreByte(img, x, y, 2, in[i++]);
      StoreByte(img, x, y, 3, in[i++]);
    }
  }
}

void RestoreHighlights(const PixWorld& src, PixWorld& dst, float threshold, float softness, float mix,
                       bool showMask) {
  int x1 = std::max(src.bounds.x1, dst.bounds.x1);
  int y1 = std::max(src.bounds.y1, dst.bounds.y1);
  int x2 = std::min(src.bounds.x2, dst.bounds.x2);
  int y2 = std::min(src.bounds.y2, dst.bounds.y2);
  const float t0 = std::max(0.f, std::min(1.f, threshold));
  const float t1 = std::max(t0, std::min(1.f, threshold + std::max(0.f, softness)));
  const float amt = std::max(0.f, std::min(1.f, mix));
  for (int y = y1; y < y2; ++y) {
    for (int x = x1; x < x2; ++x) {
      const float r = SampleByte(src, x, y, 0) / 255.f;
      const float g = SampleByte(src, x, y, 1) / 255.f;
      const float b = SampleByte(src, x, y, 2) / 255.f;
      const float peak = std::max(r, std::max(g, b));
      float w = 0.f;
      if (peak >= t1)
        w = 1.f;
      else if (peak > t0 && t1 > t0)
        w = (peak - t0) / (t1 - t0);
      w *= amt;
      if (showMask) {
        uint8_t m = (uint8_t)std::lround(w * 255.f);
        StoreByte(dst, x, y, 0, m);
        StoreByte(dst, x, y, 1, m);
        StoreByte(dst, x, y, 2, m);
        continue;
      }
      if (w <= 0.f)
        continue;
      for (int c = 0; c < 3 && c < dst.nComp; ++c) {
        float nr = SampleByte(dst, x, y, c);
        float orig = SampleByte(src, x, y, c);
        StoreByte(dst, x, y, c, (uint8_t)std::lround(nr + (orig - nr) * w));
      }
    }
  }
}

void CopyPassthrough(const PixWorld& src, PixWorld& dst) {
  int x1 = std::max(src.bounds.x1, dst.bounds.x1);
  int y1 = std::max(src.bounds.y1, dst.bounds.y1);
  int x2 = std::min(src.bounds.x2, dst.bounds.x2);
  int y2 = std::min(src.bounds.y2, dst.bounds.y2);
  for (int y = y1; y < y2; ++y) {
    for (int x = x1; x < x2; ++x) {
      for (int c = 0; c < dst.nComp; ++c)
        StoreByte(dst, x, y, c, SampleByte(src, x, y, c));
    }
  }
}

}  // namespace pix