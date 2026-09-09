#include "../plugin/pixio.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
  pix::VizDepth(out, depthv, false, 1);
  assert(dst[1] > 50 && dst[1] < 80);  // R ~= 0.25*255 in AE ARGB
}

void TestFitWorldToRowBytesClampsWidth() {
  uint8_t buf[16] = {};
  pix::PixWorld world{};
  world.data = buf;
  world.rowBytes = 16;
  world.bounds = {0, 0, 100, 1};
  world.width = 100;
  world.height = 1;
  world.nComp = 4;
  world.bytesPerComp = 1;
  assert(!pix::WorldUsable(world));
  assert(pix::FitWorldToRowBytes(world));
  assert(world.width == 4);
  assert(world.bounds.x2 == 4);
  assert(pix::WorldUsable(world));
  std::vector<uint8_t> rgba;
  pix::PackRgba8(world, rgba, false);
  assert(rgba.size() == 16);
}

void TestUnpackDoesNotReadPastPackedBuffer() {
  uint8_t dst[32] = {};
  auto world = AexWorld(dst, 2, 2, 8);
  const std::vector<uint8_t> onePixel = {10, 20, 30, 40};
  pix::UnpackRgba8(world, onePixel, false);
  assert(dst[1] == 10 && dst[2] == 20 && dst[3] == 30 && dst[0] == 40);
}

void TestCropWorldSlidesOrigin() {
  uint8_t buf[8] = {255, 1, 2, 3, 255, 9, 8, 7};
  auto world = AexWorld(buf, 2, 1, 8);
  assert(pix::CropWorld(world, {1, 0, 2, 1}));
  assert(world.width == 1 && world.height == 1);
  std::vector<uint8_t> rgba;
  pix::PackRgba8(world, rgba, false);
  assert((rgba == std::vector<uint8_t>{9, 8, 7, 255}));
}

void TestFitWorldToBytesRepairsProxyStrideLie() {
  // Houdini Copernicus proxy 1:2: 960x540 float RGBA packed tightly, but
  // kOfxImagePropRowBytes still reports the full-res 1920-wide stride.
  const int w = 960, h = 540, bpc = 4, nComp = 4;
  const int packed = w * nComp * bpc;
  const int lied = packed * 2;
  std::vector<uint8_t> buf(static_cast<size_t>(h) * packed, 0);
  float marker = 0.5f;
  std::memcpy(buf.data() + packed, &marker, 4);
  pix::PixWorld world{};
  world.data = buf.data();
  world.rowBytes = lied;
  world.bounds = {0, 0, w, h};
  world.width = w;
  world.height = h;
  world.nComp = nComp;
  world.bytesPerComp = bpc;
  assert(pix::FitWorldToBytes(world, buf.size()));
  assert(world.rowBytes == packed);
  assert(world.width == w && world.height == h);
  assert(pix::SampleNorm(world, 0, 1, 0) == 0.5f);
}

void TestHighlightOnPartialUnpackLooksLikeMask() {
  // Documents the proxy artifact: unpack only fills the top (flipped) half,
  // RestoreHighlights then paints src highlights onto the unwritten black bottom.
  float srcPix[4] = {2.f, 2.f, 2.f, 1.f};
  float dstPix[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  pix::PixWorld src{};
  src.data = reinterpret_cast<uint8_t*>(srcPix);
  src.rowBytes = 16;
  src.bounds = {0, 0, 1, 1};
  src.width = 1;
  src.height = 1;
  src.nComp = 4;
  src.bytesPerComp = 4;
  pix::PixWorld dst = src;
  dst.data = reinterpret_cast<uint8_t*>(dstPix);
  dst.bounds = {0, 0, 1, 2};
  dst.height = 2;
  const std::vector<float> packed = {0.2f, 0.2f, 0.2f, 1.f};
  pix::UnpackRgba32f(dst, packed, true);
  pix::RestoreHighlights(src, dst, 0.80f, 0.0f, 1.0f, false);
  assert(dstPix[4] == 0.2f);
  assert(dstPix[0] == 2.f);
}

void TestVizDepthStopsAtPixelCount() {
  uint8_t dst[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  auto world = AexWorld(dst, 2, 1, 8);
  float z[] = {1.f};
  pix::VizDepth(world, z, false, 1);
  assert(dst[1] == 255);
}

int main() {
  TestAexArgbTopDownRoundTrip();
  TestVerticalFlipIsExplicit();
  TestAex16BitRange();
  TestAex16BitPassthroughIsExact();
  TestHighlightProtectionWithAexArgb();
  TestHighlightProtectionUsesOriginalHdrPeak();
  TestPackMvecAndDepth();
  TestFitWorldToRowBytesClampsWidth();
  TestUnpackDoesNotReadPastPackedBuffer();
  TestCropWorldSlidesOrigin();
  TestVizDepthStopsAtPixelCount();
  TestFitWorldToBytesRepairsProxyStrideLie();
  TestHighlightOnPartialUnpackLooksLikeMask();
  std::puts("pixio_selftest: PASS");
  return 0;
}
