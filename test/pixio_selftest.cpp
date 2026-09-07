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

  std::vector<float> rgbaF;
  pix::PackRgba32f(world, rgbaF, false);
  assert(rgbaF.size() == 4);
  assert(rgbaF[0] == 1.f && rgbaF[3] == 1.f && rgbaF[2] == 0.f);
  assert(rgbaF[1] > 0.499f && rgbaF[1] < 0.501f);
}

void TestAex16BitPassthroughIsExact() {
  // 1/32768 vanishes if quantized through 8-bit; native copy must keep it.
  uint16_t src[] = {32768, 1, 2, 3};
  uint16_t dst[] = {0, 0, 0, 0};
  auto srcWorld = AexWorld(reinterpret_cast<uint8_t*>(src), 1, 1, 8, 2);
  auto dstWorld = AexWorld(reinterpret_cast<uint8_t*>(dst), 1, 1, 8, 2);
  pix::CopyPassthrough(srcWorld, dstWorld);
  assert(dst[0] == 32768 && dst[1] == 1 && dst[2] == 2 && dst[3] == 3);

  std::vector<float> packed;
  pix::PackRgba32f(srcWorld, packed, false);
  assert(packed[0] > 0.f);  // R = 1/32768, not crushed to 0
  uint16_t roundTrip[] = {0, 0, 0, 0};
  auto outWorld = AexWorld(reinterpret_cast<uint8_t*>(roundTrip), 1, 1, 8, 2);
  pix::UnpackRgba32f(outWorld, packed, false);
  assert(roundTrip[0] == 32768 && roundTrip[1] == 1 && roundTrip[2] == 2 && roundTrip[3] == 3);
  assert(pix::PackAsFloat(srcWorld));
}

void TestHighlightProtectionUsesOriginalHdrPeak() {
  float source[] = {182.625f, 0.25f, 0.125f, 1.f};
  float result[] = {0.5f, 0.5f, 0.5f, 1.f};
  pix::PixWorld srcWorld{};
  srcWorld.data = reinterpret_cast<uint8_t*>(source);
  srcWorld.rowBytes = 16;
  srcWorld.bounds = {0, 0, 1, 1};
  srcWorld.width = 1;
  srcWorld.height = 1;
  srcWorld.nComp = 4;
  srcWorld.bytesPerComp = 4;
  pix::PixWorld dstWorld = srcWorld;
  dstWorld.data = reinterpret_cast<uint8_t*>(result);
  pix::RestoreHighlights(srcWorld, dstWorld, 0.80f, 0.0f, 1.0f, false);
  assert(result[0] == 182.625f && result[1] == 0.25f && result[2] == 0.125f && result[3] == 1.f);
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

void TestPackMvecAndDepth() {
  uint8_t src[] = {255, 255, 128, 0};  // A=1, R=1, G=0.5, B=0 -> mx=(1-0.5)*2*1 = 1 pixel if w=1
  auto world = AexWorld(src, 1, 1, 4);
  std::vector<float> xy;
  pix::PackMvec(world, xy, false, false, false, true, 1.f);
  assert(xy.size() == 2);
  assert(xy[0] > 0.99f && xy[0] < 1.01f);
  assert(xy[1] > -0.01f && xy[1] < 0.01f);
  pix::PackMvec(world, xy, false, false, false, true, 64.f);
  assert(xy[0] > 63.f && xy[0] < 65.f);

  std::vector<float> z;
  pix::PackDepth(world, z, false, false);
  assert(z.size() == 1 && z[0] > 0.99f);
  pix::PackDepth(world, z, false, true);
  assert(z[0] < 0.01f);

  uint8_t dst[4] = {0, 0, 0, 0};
  auto out = AexWorld(dst, 1, 1, 4);
  float depthv[] = {0.25f};
  pix::VizDepth(out, depthv, false);
  assert(dst[1] > 50 && dst[1] < 80);  // R ~= 0.25*255 in AE ARGB
}

int main() {
  TestAexArgbTopDownRoundTrip();
  TestVerticalFlipIsExplicit();
  TestAex16BitRange();
  TestAex16BitPassthroughIsExact();
  TestHighlightProtectionWithAexArgb();
  TestHighlightProtectionUsesOriginalHdrPeak();
  TestPackMvecAndDepth();
  std::puts("pixio_selftest: PASS");
  return 0;
}
