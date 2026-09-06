#pragma once

#include <cstdint>
#include <vector>

// Shared pixel I/O layer: used by both the OFX host and the After Effects (AEX) host.
// World semantics: rows advance by rowBytes, y from bounds.y1 to y2-1;
// public channel indices are canonical R,G,B,A. channelIndex maps them to the
// host's memory layout. bytesPerComp is 1/2/4 (4 = float, unclamped).
//
// Engine interchange follows the host component size:
//   8-bpc  -> packed RGBA8
//   16-bpc / float -> packed RGBA32F (normalized 0..1 for integer depths)

namespace pix {

struct Rect {
  int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

struct PixWorld {
  uint8_t* data = nullptr;
  int rowBytes = 0;
  Rect bounds{};
  int width = 0;
  int height = 0;
  int nComp = 4;
  int bytesPerComp = 1;
  int channelIndex[4] = {0, 1, 2, 3};
  // Zero selects the default integer maximum (255/65535). AE's 16-bpc
  // PF_Pixel16 uses 32768 instead, while OFX short images use 65535.
  uint32_t maxChannelValue = 0;
};

inline bool PackAsFloat(const PixWorld& img) { return img.bytesPerComp > 1; }

// Sample channel c at (x,y). Integer depths are normalized to 0..1.
// Float worlds are returned unclamped. Out-of-range channel returns 1.
float SampleNorm(const PixWorld& img, int x, int y, int c);

// Write a normalized (typically 0..1) value with depth conversion.
void StoreNorm(PixWorld& img, int x, int y, int c, float v);

// Sample channel c at (x,y), quantized to 0..255; out-of-range channel returns 255.
uint8_t SampleByte(const PixWorld& img, int x, int y, int c);

// Write channel c at (x,y) with depth conversion; out-of-range channel ignored.
void StoreByte(PixWorld& img, int x, int y, int c, uint8_t b);

// Pack to a contiguous RGBA8 row stream (video child expects top-left rows).
// flipVert=true reads bottom-up (OFX worlds are bottom-left); false reads top-down (AE).
void PackRgba8(const PixWorld& img, std::vector<uint8_t>& out, bool flipVert);

// Write a contiguous RGBA8 row stream back into the world (mirror of PackRgba8 flipVert).
void UnpackRgba8(PixWorld& img, const std::vector<uint8_t>& in, bool flipVert);

// Pack/unpack contiguous RGBA32F (unclamped floats) using the same row order as RGBA8.
void PackRgba32f(const PixWorld& img, std::vector<float>& out, bool flipVert);
void UnpackRgba32f(PixWorld& img, const std::vector<float>& in, bool flipVert);

// Copy source world to destination world per-channel. Same-depth copies are bit-exact
// and do not round-trip through 8-bit.
void CopyPassthrough(const PixWorld& src, PixWorld& dst);

// Blend original RGB back where source peak (max RGB) exceeds threshold.
// softness is the ramp width above threshold; mix scales the mask; showMask writes the mask.
void RestoreHighlights(const PixWorld& src, PixWorld& dst, float threshold, float softness, float mix,
                       bool showMask);

// Pack R,G as motion. Integer worlds are 0.5-centered; float worlds use raw R,G.
// prevToCurrent negates; unitUv treats the value as a fraction of width/height.
void PackMvec(const PixWorld& img, std::vector<float>& xy, bool flipVert, bool prevToCurrent,
              bool unitUv, bool centerHalf, float rangePx);

// Pack R as 0..1 depth. linear=true converts near=0 to inverse (near=1).
void PackDepth(const PixWorld& img, std::vector<float>& z, bool flipVert, bool linear);

// Debug views: depth as grayscale; mvec as 0.5-centered RG (32px = full) + magnitude in B.
void VizDepth(PixWorld& dst, const float* z, bool flipVert);
void VizMvec(PixWorld& dst, const float* xy, bool flipVert);

}  // namespace pix
