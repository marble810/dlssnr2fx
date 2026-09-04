#pragma once

#include <cstdint>
#include <vector>

// Shared pixel I/O layer: used by both the OFX host and the After Effects (AEX) host.
// World semantics: rows advance by rowBytes, y from bounds.y1 to y2-1;
// public channel indices are canonical R,G,B,A. channelIndex maps them to the
// host's memory layout. bytesPerComp is 1/2/4 (4 = float 0..1).

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

// Sample channel c at (x,y), normalized to 0..255; out-of-range channel returns 255.
uint8_t SampleByte(const PixWorld& img, int x, int y, int c);

// Write channel c at (x,y) with depth conversion; out-of-range channel ignored.
void StoreByte(PixWorld& img, int x, int y, int c, uint8_t b);

// Pack to a contiguous RGBA8 row stream (video child expects top-left rows).
// flipVert=true reads bottom-up (OFX worlds are bottom-left); false reads top-down (AE).
void PackRgba8(const PixWorld& img, std::vector<uint8_t>& out, bool flipVert);

// Write a contiguous RGBA8 row stream back into the world (mirror of PackRgba8 flipVert).
void UnpackRgba8(PixWorld& img, const std::vector<uint8_t>& in, bool flipVert);

// Copy source world to destination world per-channel with depth conversion (passthrough).
void CopyPassthrough(const PixWorld& src, PixWorld& dst);

// Blend original RGB back where source peak (max RGB) exceeds threshold.
// softness is the ramp width above threshold; mix scales the mask; showMask writes the mask.
void RestoreHighlights(const PixWorld& src, PixWorld& dst, float threshold, float softness, float mix,
                       bool showMask);

}  // namespace pix