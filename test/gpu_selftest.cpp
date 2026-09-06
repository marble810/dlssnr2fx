#include "../engine/gpu.h"

#include <cmath>
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
  std::vector<float> inputF(static_cast<size_t>(width) * height * 4, 0.5f);
  inputF[0] = 1.0f / 512.0f;  // rounds to 0 in RGBA8, stays finite in RGBA16F
  inputF[3] = 1.0f;
  if (!gpu.UploadInputs(inputF.data(), inputF.size() * sizeof(float), 5000)) return 6;
  if (!gpu.CopyColorToOutput(5000)) return 7;
  std::vector<float> outputF;
  if (!gpu.ReadOutput(outputF, 5000) || outputF.size() != inputF.size()) return 8;
  if (std::fabs(outputF[0] - inputF[0]) > 1.0e-5f) return 9;
  if (std::fabs(outputF[3] - 1.0f) > 1.0e-3f) return 10;
  gpu.Shutdown();
  std::printf("gpu_selftest: PASS (%dx%d RGBA8/RGBA32F -> RGBA16F)\n", width, height);
  return 0;
}
