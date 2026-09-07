#include "mvec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr int kBlock = 8;
constexpr int kSearch = 16;
constexpr int kMaxLongEdge = 320;

void LumaRgba8(const uint8_t* rgba, int w, int h, std::vector<uint8_t>& luma) {
  luma.resize(static_cast<size_t>(w) * h);
  for (int i = 0; i < w * h; ++i) {
    const uint8_t* p = rgba + i * 4;
    luma[i] = static_cast<uint8_t>((p[0] * 54 + p[1] * 183 + p[2] * 19) >> 8);
  }
}

void LumaRgba32f(const float* rgba, int w, int h, std::vector<uint8_t>& luma) {
  luma.resize(static_cast<size_t>(w) * h);
  for (int i = 0; i < w * h; ++i) {
    const float* p = rgba + i * 4;
    float r = std::isfinite(p[0]) ? p[0] : 0.f;
    float g = std::isfinite(p[1]) ? p[1] : 0.f;
    float b = std::isfinite(p[2]) ? p[2] : 0.f;
    float y = r * 0.2126f + g * 0.7152f + b * 0.0722f;
    y = std::max(0.f, std::min(1.f, y));
    luma[i] = static_cast<uint8_t>(y * 255.f + 0.5f);
  }
}

void Downsample(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh) {
  for (int y = 0; y < dh; ++y) {
    const int y0 = y * sh / dh;
    const int y1 = std::max(y0 + 1, (y + 1) * sh / dh);
    for (int x = 0; x < dw; ++x) {
      const int x0 = x * sw / dw;
      const int x1 = std::max(x0 + 1, (x + 1) * sw / dw);
      int sum = 0, n = 0;
      for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) {
          sum += src[yy * sw + xx];
          ++n;
        }
      dst[y * dw + x] = static_cast<uint8_t>(n ? sum / n : 0);
    }
  }
}

int Sad(const uint8_t* curr, const uint8_t* prev, int w, int h,
        int cx, int cy, int dx, int dy, int bw, int bh) {
  int s = 0;
  for (int y = 0; y < bh; ++y) {
    const int y0 = cy + y;
    const int y1 = cy + dy + y;
    if (y0 < 0 || y1 < 0 || y0 >= h || y1 >= h) {
      s += 255 * bw;
      continue;
    }
    const uint8_t* a = curr + y0 * w + cx;
    const uint8_t* b = prev + y1 * w + cx + dx;
    for (int x = 0; x < bw; ++x) {
      const int x0 = cx + x;
      const int x1 = cx + dx + x;
      if (x0 < 0 || x1 < 0 || x0 >= w || x1 >= w)
        s += 255;
      else
        s += std::abs(static_cast<int>(a[x]) - static_cast<int>(b[x]));
    }
  }
  return s;
}

void MatchGrid(const uint8_t* curr, const uint8_t* prev, int w, int h,
               std::vector<float>& grid, int& gw, int& gh) {
  gw = std::max(1, w / kBlock);
  gh = std::max(1, h / kBlock);
  grid.assign(static_cast<size_t>(gw) * gh * 2, 0.f);
  const int bw = std::max(1, w / gw);
  const int bh = std::max(1, h / gh);
  for (int gy = 0; gy < gh; ++gy) {
    for (int gx = 0; gx < gw; ++gx) {
      const int cx = gx * w / gw;
      const int cy = gy * h / gh;
      int best = 0x7fffffff, bdx = 0, bdy = 0;
      for (int dy = -kSearch; dy <= kSearch; ++dy) {
        for (int dx = -kSearch; dx <= kSearch; ++dx) {
          const int s = Sad(curr, prev, w, h, cx, cy, dx, dy, bw, bh);
          if (s < best) {
            best = s;
            bdx = dx;
            bdy = dy;
          }
        }
      }
      const size_t i = (static_cast<size_t>(gy) * gw + gx) * 2;
      grid[i] = static_cast<float>(bdx);
      grid[i + 1] = static_cast<float>(bdy);
    }
  }
}

void Upsample(const std::vector<float>& grid, int gw, int gh,
              int w, int h, float scaleX, float scaleY, std::vector<float>& xy) {
  xy.resize(static_cast<size_t>(w) * h * 2);
  for (int y = 0; y < h; ++y) {
    const float gy = (gh == 1) ? 0.f : (static_cast<float>(y) + 0.5f) * gh / h - 0.5f;
    const int y0 = std::max(0, std::min(gh - 1, (int)std::floor(gy)));
    const int y1 = std::max(0, std::min(gh - 1, y0 + 1));
    const float ty = gy - y0;
    for (int x = 0; x < w; ++x) {
      const float gx = (gw == 1) ? 0.f : (static_cast<float>(x) + 0.5f) * gw / w - 0.5f;
      const int x0 = std::max(0, std::min(gw - 1, (int)std::floor(gx)));
      const int x1 = std::max(0, std::min(gw - 1, x0 + 1));
      const float tx = gx - x0;
      auto at = [&](int ix, int iy) -> const float* {
        return grid.data() + (static_cast<size_t>(iy) * gw + ix) * 2;
      };
      const float* a = at(x0, y0);
      const float* b = at(x1, y0);
      const float* c = at(x0, y1);
      const float* d = at(x1, y1);
      const float dx = (a[0] * (1 - tx) + b[0] * tx) * (1 - ty) + (c[0] * (1 - tx) + d[0] * tx) * ty;
      const float dy = (a[1] * (1 - tx) + b[1] * tx) * (1 - ty) + (c[1] * (1 - tx) + d[1] * tx) * ty;
      const size_t i = (static_cast<size_t>(y) * w + x) * 2;
      xy[i] = dx * scaleX;
      xy[i + 1] = dy * scaleY;
    }
  }
}

void EstimateFromLuma(const std::vector<uint8_t>& currFull, const std::vector<uint8_t>& prevFull,
                      int w, int h, std::vector<float>& xy) {
  int cw = w, ch = h;
  const int longEdge = std::max(w, h);
  if (longEdge > kMaxLongEdge) {
    const float s = kMaxLongEdge / static_cast<float>(longEdge);
    cw = std::max(1, (int)std::lround(w * s));
    ch = std::max(1, (int)std::lround(h * s));
  }
  std::vector<uint8_t> curr = currFull, prev = prevFull;
  if (cw != w || ch != h) {
    curr.resize(static_cast<size_t>(cw) * ch);
    prev.resize(static_cast<size_t>(cw) * ch);
    Downsample(currFull.data(), w, h, curr.data(), cw, ch);
    Downsample(prevFull.data(), w, h, prev.data(), cw, ch);
  }
  std::vector<float> grid;
  int gw = 0, gh = 0;
  MatchGrid(curr.data(), prev.data(), cw, ch, grid, gw, gh);
  Upsample(grid, gw, gh, w, h, w / static_cast<float>(cw), h / static_cast<float>(ch), xy);
}

}  // namespace

void EstimateMvecRgba8(const uint8_t* current, const uint8_t* previous,
                       int width, int height, std::vector<float>& xy) {
  if (!current || !previous || width <= 0 || height <= 0) {
    xy.clear();
    return;
  }
  std::vector<uint8_t> curr, prev;
  LumaRgba8(current, width, height, curr);
  LumaRgba8(previous, width, height, prev);
  EstimateFromLuma(curr, prev, width, height, xy);
}

void EstimateMvecRgba32f(const float* current, const float* previous,
                         int width, int height, std::vector<float>& xy) {
  if (!current || !previous || width <= 0 || height <= 0) {
    xy.clear();
    return;
  }
  std::vector<uint8_t> curr, prev;
  LumaRgba32f(current, width, height, curr);
  LumaRgba32f(previous, width, height, prev);
  EstimateFromLuma(curr, prev, width, height, xy);
}
