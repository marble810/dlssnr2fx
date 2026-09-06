#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

struct GpuTexture {
  Microsoft::WRL::ComPtr<ID3D12Resource> resource;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
  uint32_t width = 0;
  uint32_t height = 0;
};

class GpuContext {
public:
  ~GpuContext();
  bool Initialize(int adapter);
  void Shutdown();
  bool CreateFrameResources(uint32_t width, uint32_t height);
  bool UploadInputs(const uint8_t* rgba, size_t rgbaBytes, uint32_t timeoutMs);
  bool UploadInputs(const float* rgba, size_t rgbaBytes, uint32_t timeoutMs);
  bool UploadInputs(const uint8_t* rgba, size_t rgbaBytes, const float* mvecXY, size_t mvecBytes,
                    uint32_t timeoutMs);
  bool UploadInputs(const float* rgba, size_t rgbaBytes, const float* mvecXY, size_t mvecBytes,
                    uint32_t timeoutMs);
  bool UploadInputs(const uint8_t* rgba, size_t rgbaBytes, const float* mvecXY, size_t mvecBytes,
                    const float* depthR, size_t depthBytes, uint32_t timeoutMs);
  bool UploadInputs(const float* rgba, size_t rgbaBytes, const float* mvecXY, size_t mvecBytes,
                    const float* depthR, size_t depthBytes, uint32_t timeoutMs);
  bool Begin();
  bool EndAndWait(uint32_t timeoutMs);
  bool ReadOutput(std::vector<uint8_t>& rgba, uint32_t timeoutMs);
  bool ReadOutput(std::vector<float>& rgba, uint32_t timeoutMs);
  // Verification path: copies uploaded color to output without invoking NGX.
  bool CopyColorToOutput(uint32_t timeoutMs);

  ID3D12Device* Device() const { return device_.Get(); }
  ID3D12GraphicsCommandList* List() const { return list_.Get(); }
  ID3D12Resource* Color() const { return color_.resource.Get(); }
  ID3D12Resource* Depth() const { return depth_.resource.Get(); }
  ID3D12Resource* Motion() const { return motion_.resource.Get(); }
  ID3D12Resource* Output() const { return output_.resource.Get(); }
  uint32_t Width() const { return color_.width; }
  uint32_t Height() const { return color_.height; }
  LUID AdapterLuid() const { return adapterLuid_; }

private:
  bool CreateTexture(GpuTexture& texture, uint32_t width, uint32_t height, DXGI_FORMAT format);
  bool UploadTexture(GpuTexture& texture, const void* rows, size_t rowBytes,
                     D3D12_RESOURCE_STATES finalState, uint32_t timeoutMs);
  bool UploadHalfColorAndDummies(const std::vector<uint16_t>& color, const uint16_t* motionRG,
                                 const float* depthR, uint32_t timeoutMs);
  bool ReadbackHalf(std::vector<uint16_t>& halfRgba, uint32_t timeoutMs);
  bool Transition(GpuTexture& texture, D3D12_RESOURCE_STATES state);
  bool Wait(uint32_t timeoutMs);

  Microsoft::WRL::ComPtr<IDXGIFactory6> factory_;
  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list_;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  HANDLE fenceEvent_ = nullptr;
  uint64_t fenceValue_ = 0;
  bool listOpen_ = false;
  LUID adapterLuid_{};
  GpuTexture color_, depth_, motion_, output_;
};
