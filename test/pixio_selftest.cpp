#include "../plugin/pixio.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

pix::PixWorld AexWorld(uint8_t* data, int width, int height, int rowBytes, int bytesPerComp = 1) {
  pix::PixWorld world;
  world.data = data;
  world.rowBytes = rowBytes;
  world.bounds = {0, 0, width, height};
  world.width = width;
  world.height = height;
  world.nComp = 4;
  world.bytesPerComp = bytesPerComp;
  world.channelIndex[0] = 1;  // R in AE's A,R,G,B memory layout
  world.channelIndex[1] = 2;
  world.channelIndex[2] = 3;
  world.channelIndex[3] = 0;
  if (bytesPerComp == 2)
    world.maxChannelValue = 32768;
  return world;
}

void TestAexArgbTopDownRoundTrip() {
  // Two top-down rows of A,R,G,B pixels. Padding verifies rowBytes handling.
  uint8_t src[] = {
      40, 10, 20, 30, 80, 50, 60, 70, 0xcc, 0xcc,
      120, 90, 100, 110, 160, 130, 140, 150, 0xdd, 0xdd,
  };
  uint8_t dst[sizeof(src)] = {};
  auto srcWorld = AexWorld(src, 2, 2, 10);
  auto dstWorld = AexWorld(dst, 2, 2, 10);

  std::vector<uint8_t> rgba;
  pix::PackRgba8(srcWorld, rgba, false);
  const std::vector<uint8_t> expected = {
      10, 20, 30, 40, 50, 60, 70, 80,
      90, 100, 110, 120, 130, 140, 150, 160,
  };
  assert(rgba == expected);

  pix::UnpackRgba8(dstWorld, rgba, false);
  for (int row = 0; row < 2; ++row) {
    for (int i = 0; i < 8; ++i)
      assert(dst[row * 10 + i] == src[row * 10 + i]);
  }
}

void TestVerticalFlipIsExplicit() {
  uint8_t src[] = {
      255, 1, 2, 3,
      255, 4, 5, 6,
  };
  auto world = AexWorld(src, 1, 2, 4);
  std::vector<uint8_t> rgba;
  pix::PackRgba8(world, rgba, true);
  const std::vector<uint8_t> expected = {4, 5, 6, 255, 1, 2, 3, 255};
  assert(rgba == expected);
}

void TestAex16BitRange() {
  uint16_t pixel[] = {32768, 32768, 16384, 0};  // A=1, R=1, G=.5, B=0
  auto world = AexWorld(reinterpret_cast<uint8_t*>(pixel), 1, 1, 8, 2);
  std::vector<uint8_t> rgba;
  pix::PackRgba8(world, rgba, false);
  assert((rgba == std::vector<uint8_t>{255, 128, 0, 255}));
}

void TestHighlightProtectionWithAexArgb() {
  // Pixel 0 is above the threshold; pixel 1 is below it.
  uint8_t source[] = {255, 240, 220, 200, 255, 80, 70, 60};
  uint8_t result[] = {255, 120, 110, 100, 255, 40, 35, 30};
  auto srcWorld = AexWorld(source, 2, 1, 8);
  auto dstWorld = AexWorld(result, 2, 1, 8);

  pix::RestoreHighlights(srcWorld, dstWorld, 0.80f, 0.0f, 1.0f, false);
  assert(result[1] == 240 && result[2] == 220 && result[3] == 200);
  assert(result[5] == 40 && result[6] == 35 && result[7] == 30);

  pix::RestoreHighlights(srcWorld, dstWorld, 0.80f, 0.0f, 1.0f, true);
  assert(result[1] == 255 && result[2] == 255 && result[3] == 255);
  assert(result[5] == 0 && result[6] == 0 && result[7] == 0);
}

}  // namespace

int main() {
  TestAexArgbTopDownRoundTrip();
  TestVerticalFlipIsExplicit();
  TestAex16BitRange();
  TestHighlightProtectionWithAexArgb();
  std::puts("pixio_selftest: PASS");
  return 0;
}
