#include "pixio.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pix {

bool WorldUsable(const PixWorld& img) {
  if (!img.data || img.width <= 0 || img.height <= 0) return false;
  if (img.nComp <= 0 || img.bytesPerComp <= 0 || img.rowBytes == 0) return false;
  const int64_t rowNeed = int64_t(img.width) * img.nComp * img.bytesPerComp;
  return std::abs(int64_t(img.rowBytes)) >= rowNeed;
}

bool FitWorldToRowBytes(PixWorld& img) {
  if (!img.data || img.width <= 0 || img.height <= 0) return false;
  if (img.nComp <= 0 || img.bytesPerComp <= 0 || img.rowBytes == 0) return false;
  const int64_t pixBytes = int64_t(img.nComp) * img.bytesPerComp;
  const int64_t stride = std::abs(int64_t(img.rowBytes));
  if (pixBytes <= 0 || stride < pixBytes) return false;
  const int maxW = static_cast<int>(stride / pixBytes);
  if (img.width > maxW) {
    img.width = maxW;
    img.bounds.x2 = img.bounds.x1 + maxW;
  }
  return img.width > 0 && img.height > 0;
}

bool CropWorld(PixWorld& img, Rect win) {
  const int x1 = std::max(img.bounds.x1, win.x1);
  const int y1 = std::max(img.bounds.y1, win.y1);
  const int x2 = std::min(img.bounds.x2, win.x2);
  const int y2 = std::min(img.bounds.y2, win.y2);
  if (x2 <= x1 || y2 <= y1 || !img.data) return false;
  img.data += (y1 - img.bounds.y1) * static_cast<ptrdiff_t>(img.rowBytes) +
              (x1 - img.bounds.x1) * img.nComp * img.bytesPerComp;
  img.bounds = {x1, y1, x2, y2};
  img.width = x2 - x1;
  img.height = y2 - y1;
  return FitWorldToRowBytes(img);
}

bool FitWorldToBytes(PixWorld& img, size_t avail) {
  if (!img.data || img.width <= 0 || img.height <= 0) return false;
  if (img.nComp <= 0 || img.bytesPerComp <= 0) return false;
  if (!FitWorldToRowBytes(img)) return false;
  const int64_t rowW = int64_t(img.width) * img.nComp * img.bytesPerComp;
  const int sign = img.rowBytes < 0 ? -1 : 1;
  const int64_t stride = std::abs(int64_t(img.rowBytes));
  const size_t claimed =
      static_cast<size_t>(img.height - 1) * static_cast<size_t>(stride) + static_cast<size_t>(rowW);
  if (avail >= claimed) return true;
  const size_t packedSpan = static_cast<size_t>(img.height) * static_cast<size_t>(rowW);
  if (avail >= packedSpan) {
    img.rowBytes = static_cast<int>(sign * rowW);
    return true;
  }
  const int maxH = static_cast<int>(avail / static_cast<size_t>(rowW));
  if (maxH <= 0) return false;
  img.rowBytes = static_cast<int>(sign * rowW);
  img.height = maxH;
  img.bounds.y2 = img.bounds.y1 + maxH;
  return WorldUsable(img);
}

namespace {

inline uint8_t* PixelPtr(const PixWorld& img, int x, int y, int c) {
  return img.data + (y - img.bounds.y1) * img.rowBytes +
         (x - img.bounds.x1) * img.nComp * img.bytesPerComp +
         img.channelIndex[c] * img.bytesPerComp;
}

uint32_t IntegerMax(const PixWorld& img) {
  if (img.maxChannelValue) return img.maxChannelValue;
  return img.bytesPerComp == 1 ? 255u : 65535u;
}

}  // namespace

float SampleNorm(const PixWorld& img, int x, int y, int c) {
  if (c >= img.nComp)
    return 1.f;
  const uint8_t* p = PixelPtr(img, x, y, c);
  if (img.bytesPerComp == 1)
    return *p / 255.f;
  if (img.bytesPerComp == 2) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    const uint32_t maxValue = IntegerMax(img);
    return std::min<uint32_t>(v, maxValue) / static_cast<float>(maxValue);
  }
  float f;
  std::memcpy(&f, p, 4);
  return f;
}

void StoreNorm(PixWorld& img, int x, int y, int c, float v) {
  if (c >= img.nComp)
    return;
  uint8_t* p = PixelPtr(img, x, y, c);
  if (img.bytesPerComp == 1) {
    int b = (int)std::lround(std::max(0.f, std::min(1.f, v)) * 255.f);
    *p = (uint8_t)b;
  } else if (img.bytesPerComp == 2) {
    const uint32_t maxValue = IntegerMax(img);
    float t = std::max(0.f, std::min(1.f, v));
    uint16_t s = (uint16_t)std::lround(t * maxValue);
    std::memcpy(p, &s, 2);
  } else {
    std::memcpy(p, &v, 4);
  }
}

uint8_t SampleByte(const PixWorld& img, int x, int y, int c) {
  if (c >= img.nComp)
    return 255;
  if (img.bytesPerComp == 1)
    return *PixelPtr(img, x, y, c);
  return (uint8_t)std::lround(std::max(0.f, std::min(1.f, SampleNorm(img, x, y, c))) * 255.f);
}

void StoreByte(PixWorld& img, int x, int y, int c, uint8_t b) {
  if (img.bytesPerComp == 1) {
    if (c >= img.nComp)
      return;
    *PixelPtr(img, x, y, c) = b;
    return;
  }
  StoreNorm(img, x, y, c, b / 255.f);
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
  const size_t n = in.size();
  const int yStart = flipVert ? img.bounds.y2 - 1 : img.bounds.y1;
  const int yEnd = flipVert ? img.bounds.y1 - 1 : img.bounds.y2;
  const int yStep = flipVert ? -1 : 1;
  for (int y = yStart; y != yEnd; y += yStep) {
    for (int x = img.bounds.x1; x < img.bounds.x2; ++x) {
      if (i + 4 > n) return;
      StoreByte(img, x, y, 0, in[i++]);
      StoreByte(img, x, y, 1, in[i++]);
      StoreByte(img, x, y, 2, in[i++]);
      StoreByte(img, x, y, 3, in[i++]);
    }
  }
}

void PackRgba32f(const PixWorld& img, std::vector<float>& out, bool flipVert) {
  out.resize((size_t)img.width * img.height * 4);
  size_t i = 0;
  const int yStart = flipVert ? img.bounds.y2 - 1 : img.bounds.y1;
  const int yEnd = flipVert ? img.bounds.y1 - 1 : img.bounds.y2;
  const int yStep = flipVert ? -1 : 1;
  for (int y = yStart; y != yEnd; y += yStep) {
    for (int x = img.bounds.x1; x < img.bounds.x2; ++x) {
      out[i++] = SampleNorm(img, x, y, 0);
      out[i++] = SampleNorm(img, x, y, 1);
      out[i++] = SampleNorm(img, x, y, 2);
      out[i++] = SampleNorm(img, x, y, 3);
    }
  }
}

void UnpackRgba32f(PixWorld& img, const std::vector<float>& in, bool flipVert) {
  size_t i = 0;
  const size_t n = in.size();
  const int yStart = flipVert ? img.bounds.y2 - 1 : img.bounds.y1;
  const int yEnd = flipVert ? img.bounds.y1 - 1 : img.bounds.y2;
  const int yStep = flipVert ? -1 : 1;
  for (int y = yStart; y != yEnd; y += yStep) {
    for (int x = img.bounds.x1; x < img.bounds.x2; ++x) {
      if (i + 4 > n) return;
      StoreNorm(img, x, y, 0, in[i++]);
      StoreNorm(img, x, y, 1, in[i++]);
      StoreNorm(img, x, y, 2, in[i++]);
      StoreNorm(img, x, y, 3, in[i++]);
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
      const float r = SampleNorm(src, x, y, 0);
      const float g = SampleNorm(src, x, y, 1);
      const float b = SampleNorm(src, x, y, 2);
      const float peak = std::max(r, std::max(g, b));
      float w = 0.f;
      if (peak >= t1)
        w = 1.f;
      else if (peak > t0 && t1 > t0)
        w = (peak - t0) / (t1 - t0);
      w *= amt;
      if (showMask) {
        StoreNorm(dst, x, y, 0, w);
        StoreNorm(dst, x, y, 1, w);
        StoreNorm(dst, x, y, 2, w);
        continue;
      }
      if (w <= 0.f)
        continue;
      for (int c = 0; c < 3 && c < dst.nComp; ++c) {
        float nr = SampleNorm(dst, x, y, c);
        float orig = SampleNorm(src, x, y, c);
        StoreNorm(dst, x, y, c, nr + (orig - nr) * w);
      }
    }
  }
}

void PackMvec(const PixWorld& img, std::vector<float>& xy, bool flipVert, bool prevToCurrent,
              bool unitUv, bool centerHalf, float rangePx) {
  xy.resize((size_t)img.width * img.height * 2);
  size_t i = 0;
  const int yStart = flipVert ? img.bounds.y2 - 1 : img.bounds.y1;
  const int yEnd = flipVert ? img.bounds.y1 - 1 : img.bounds.y2;
  const int yStep = flipVert ? -1 : 1;
  // Integer worlds cannot store signed pixels; always 0.5-centered.
  const bool centered = centerHalf || img.bytesPerComp < 4;
  const float range = rangePx > 0.f ? rangePx : 64.f;
  const float sign = prevToCurrent ? -1.f : 1.f;
  for (int y = yStart; y != yEnd; y += yStep) {
    for (int x = img.bounds.x1; x < img.bounds.x2; ++x) {
      float mx = SampleNorm(img, x, y, 0);
      float my = img.nComp > 1 ? SampleNorm(img, x, y, 1) : 0.f;
      if (centered) {
        mx = (mx - 0.5f) * 2.f;
        my = (my - 0.5f) * 2.f;
        if (unitUv) {
          mx *= static_cast<float>(img.width);
          my *= static_cast<float>(img.height);
        } else {
          mx *= range;
          my *= range;
        }
      } else if (unitUv) {
        mx *= static_cast<float>(img.width);
        my *= static_cast<float>(img.height);
      }
      xy[i++] = mx * sign;
      xy[i++] = my * sign;
    }
  }
}

void PackDepth(const PixWorld& img, std::vector<float>& z, bool flipVert, bool linear) {
  z.resize((size_t)img.width * img.height);
  size_t i = 0;
  const int yStart = flipVert ? img.bounds.y2 - 1 : img.bounds.y1;
  const int yEnd = flipVert ? img.bounds.y1 - 1 : img.bounds.y2;
  const int yStep = flipVert ? -1 : 1;
  for (int y = yStart; y != yEnd; y += yStep) {
    for (int x = img.bounds.x1; x < img.bounds.x2; ++x) {
      float d = SampleNorm(img, x, y, 0);
      if (img.bytesPerComp == 4) {
        if (!std::isfinite(d)) d = 0.f;
        if (linear) d = std::max(0.f, std::min(1.f, d));
      } else {
        d = std::max(0.f, std::min(1.f, d));
      }
      z[i++] = linear ? 1.f - d : d;
    }
  }
}

void VizDepth(PixWorld& dst, const float* z, bool flipVert, size_t nPixels) {
  if (!z) return;
  size_t i = 0;
  const int yStart = flipVert ? dst.bounds.y2 - 1 : dst.bounds.y1;
  const int yEnd = flipVert ? dst.bounds.y1 - 1 : dst.bounds.y2;
  const int yStep = flipVert ? -1 : 1;
  for (int y = yStart; y != yEnd; y += yStep) {
    for (int x = dst.bounds.x1; x < dst.bounds.x2; ++x) {
      if (i >= nPixels) return;
      float v = std::max(0.f, std::min(1.f, z[i++]));
      StoreNorm(dst, x, y, 0, v);
      StoreNorm(dst, x, y, 1, v);
      StoreNorm(dst, x, y, 2, v);
      StoreNorm(dst, x, y, 3, 1.f);
    }
  }
}

void VizMvec(PixWorld& dst, const float* xy, bool flipVert, size_t nPixels) {
  if (!xy) return;
  constexpr float kScale = 32.f;
  size_t i = 0;
  const int yStart = flipVert ? dst.bounds.y2 - 1 : dst.bounds.y1;
  const int yEnd = flipVert ? dst.bounds.y1 - 1 : dst.bounds.y2;
  const int yStep = flipVert ? -1 : 1;
  for (int y = yStart; y != yEnd; y += yStep) {
    for (int x = dst.bounds.x1; x < dst.bounds.x2; ++x) {
      if (i / 2 >= nPixels) return;
      float dx = xy[i++];
      float dy = xy[i++];
      if (!std::isfinite(dx)) dx = 0.f;
      if (!std::isfinite(dy)) dy = 0.f;
      float mag = std::sqrt(dx * dx + dy * dy) / kScale;
      StoreNorm(dst, x, y, 0, std::max(0.f, std::min(1.f, 0.5f + dx / kScale)));
      StoreNorm(dst, x, y, 1, std::max(0.f, std::min(1.f, 0.5f + dy / kScale)));
      StoreNorm(dst, x, y, 2, std::max(0.f, std::min(1.f, mag)));
      StoreNorm(dst, x, y, 3, 1.f);
    }
  }
}

void CopyPassthrough(const PixWorld& src, PixWorld& dst) {
  int x1 = std::max(src.bounds.x1, dst.bounds.x1);
  int y1 = std::max(src.bounds.y1, dst.bounds.y1);
  int x2 = std::min(src.bounds.x2, dst.bounds.x2);
  int y2 = std::min(src.bounds.y2, dst.bounds.y2);
  const bool sameDepth = src.bytesPerComp == dst.bytesPerComp && src.bytesPerComp > 0;
  for (int y = y1; y < y2; ++y) {
    for (int x = x1; x < x2; ++x) {
      for (int c = 0; c < dst.nComp; ++c) {
        if (c >= src.nComp) {
          StoreNorm(dst, x, y, c, 1.f);
          continue;
        }
        if (sameDepth)
          std::memcpy(PixelPtr(dst, x, y, c), PixelPtr(src, x, y, c), src.bytesPerComp);
        else
          StoreNorm(dst, x, y, c, SampleNorm(src, x, y, c));
      }
    }
  }
}

}  // namespace pix
