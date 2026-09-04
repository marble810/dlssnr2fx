#include "../engine/gpu.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

int main() {
  GpuContext gpu;
  if (!gpu.Initialize(-1)) {
    std::puts("gpu_selftest: SKIP (no hardware D3D12 adapter)");
    return 0;
  }
  const int width = 7, height = 3;
  if (!gpu.CreateFrameResources(width, height)) return 1;
  std::vector<uint8_t> input(static_cast<size_t>(width) * height * 4);
  for (size_t i = 0; i < input.size(); i += 4) {
    input[i] = 32; input[i + 1] = 96; input[i + 2] = 192; input[i + 3] = 255;
  }
  if (!gpu.UploadInputs(input.data(), input.size(), 5000)) return 2;
  if (!gpu.CopyColorToOutput(5000)) return 3;
  std::vector<uint8_t> output;
  if (!gpu.ReadOutput(output, 5000) || output.size() != input.size()) return 4;
  for (size_t i = 0; i < input.size(); ++i)
    if (std::abs(static_cast<int>(input[i]) - static_cast<int>(output[i])) > 1) return 5;
  gpu.Shutdown();
  std::printf("gpu_selftest: PASS (%dx%d RGBA8 -> RGBA16F -> RGBA8)\n", width, height);
  return 0;
}
