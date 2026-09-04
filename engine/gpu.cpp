#include "gpu.h"

#include <DirectXPackedVector.h>

#include <algorithm>
#include <cstring>
#include <limits>

using Microsoft::WRL::ComPtr;
using DirectX::PackedVector::XMConvertFloatToHalf;
using DirectX::PackedVector::XMConvertHalfToFloat;

namespace {
D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES h{};
  h.Type = type;
  h.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  h.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  h.CreationNodeMask = 1;
  h.VisibleNodeMask = 1;
  return h;
}

D3D12_RESOURCE_DESC BufferDesc(uint64_t bytes) {
  D3D12_RESOURCE_DESC d{};
  d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  d.Width = bytes;
  d.Height = 1;
  d.DepthOrArraySize = 1;
  d.MipLevels = 1;
  d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return d;
}
}  // namespace

GpuContext::~GpuContext() { Shutdown(); }

bool GpuContext::Initialize(int adapterIndex) {
  Shutdown();
  if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)))) return false;

  ComPtr<IDXGIAdapter1> chosen;
  int usable = 0;
  for (UINT i = 0;; ++i) {
    ComPtr<IDXGIAdapter1> adapter;
    if (factory_->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                             IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND)
      break;
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                 __uuidof(ID3D12Device), nullptr)))
      continue;
    if (adapterIndex >= 0 && usable++ != adapterIndex) continue;
    chosen = adapter;
    break;
  }
  if (!chosen || FAILED(D3D12CreateDevice(chosen.Get(), D3D_FEATURE_LEVEL_11_0,
                                           IID_PPV_ARGS(&device_))))
    return false;

  D3D12_COMMAND_QUEUE_DESC q{};
  q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(device_->CreateCommandQueue(&q, IID_PPV_ARGS(&queue_))) ||
      FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&allocator_))) ||
      FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(),
                                        nullptr, IID_PPV_ARGS(&list_))) ||
      FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
    Shutdown();
    return false;
  }
  list_->Close();
  fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!fenceEvent_) {
    Shutdown();
    return false;
  }
  return true;
}

void GpuContext::Shutdown() {
  bool safe = true;
  if (queue_ && fence_) {
    const uint64_t value = ++fenceValue_;
    if (FAILED(queue_->Signal(fence_.Get(), value))) safe = false;
    else if (fence_->GetCompletedValue() < value) {
      if (FAILED(fence_->SetEventOnCompletion(value, fenceEvent_)) ||
          WaitForSingleObject(fenceEvent_, 2000) != WAIT_OBJECT_0)
        safe = false;
    }
  }
  if (!safe) {
    // A wedged driver must not turn process teardown into an unbounded wait.
    // Keep GPU-owned objects alive rather than releasing resources still in use.
    color_.resource.Detach(); depth_.resource.Detach(); motion_.resource.Detach(); output_.resource.Detach();
    list_.Detach(); allocator_.Detach(); queue_.Detach(); fence_.Detach(); device_.Detach(); factory_.Detach();
  } else {
    color_ = {}; depth_ = {}; motion_ = {}; output_ = {};
    list_.Reset(); allocator_.Reset(); queue_.Reset(); fence_.Reset(); device_.Reset(); factory_.Reset();
  }
  if (fenceEvent_) CloseHandle(fenceEvent_);
  fenceEvent_ = nullptr;
  listOpen_ = false;
  fenceValue_ = 0;
}

bool GpuContext::CreateTexture(GpuTexture& texture, uint32_t width, uint32_t height,
                               DXGI_FORMAT format) {
  D3D12_RESOURCE_DESC d{};
  d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  d.Width = width;
  d.Height = height;
  d.DepthOrArraySize = 1;
  d.MipLevels = 1;
  d.Format = format;
  d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  auto heap = Heap(D3D12_HEAP_TYPE_DEFAULT);
  texture = {};
  if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
                                               D3D12_RESOURCE_STATE_COMMON, nullptr,
                                               IID_PPV_ARGS(&texture.resource))))
    return false;
  texture.format = format;
  texture.state = D3D12_RESOURCE_STATE_COMMON;
  texture.width = width;
  texture.height = height;
  return true;
}

bool GpuContext::CreateFrameResources(uint32_t width, uint32_t height) {
  if (!device_ || width == 0 || height == 0) return false;
  return CreateTexture(color_, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT) &&
         CreateTexture(depth_, width, height, DXGI_FORMAT_R32_FLOAT) &&
         CreateTexture(motion_, width, height, DXGI_FORMAT_R16G16_FLOAT) &&
         CreateTexture(output_, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
}

bool GpuContext::Begin() {
  if (!allocator_ || !list_ || listOpen_) return false;
  if (FAILED(allocator_->Reset()) || FAILED(list_->Reset(allocator_.Get(), nullptr))) return false;
  listOpen_ = true;
  return true;
}

bool GpuContext::Wait(uint32_t timeoutMs) {
  const uint64_t value = ++fenceValue_;
  if (FAILED(queue_->Signal(fence_.Get(), value))) return false;
  if (fence_->GetCompletedValue() >= value) return true;
  if (FAILED(fence_->SetEventOnCompletion(value, fenceEvent_))) return false;
  return WaitForSingleObject(fenceEvent_, timeoutMs ? timeoutMs : 1) == WAIT_OBJECT_0;
}

bool GpuContext::EndAndWait(uint32_t timeoutMs) {
  if (!listOpen_) return false;
  listOpen_ = false;
  if (FAILED(list_->Close())) return false;
  ID3D12CommandList* lists[] = {list_.Get()};
  queue_->ExecuteCommandLists(1, lists);
  return Wait(timeoutMs);
}

bool GpuContext::Transition(GpuTexture& texture, D3D12_RESOURCE_STATES state) {
  if (texture.state == state) return true;
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = texture.resource.Get();
  b.Transition.StateBefore = texture.state;
  b.Transition.StateAfter = state;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list_->ResourceBarrier(1, &b);
  texture.state = state;
  return true;
}

bool GpuContext::UploadTexture(GpuTexture& texture, const void* rows, size_t rowBytes,
                               D3D12_RESOURCE_STATES finalState, uint32_t timeoutMs) {
  D3D12_RESOURCE_DESC td = texture.resource->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
  UINT numRows = 0;
  UINT64 rowSize = 0, total = 0;
  device_->GetCopyableFootprints(&td, 0, 1, 0, &fp, &numRows, &rowSize, &total);
  if (rowBytes > rowSize || numRows != texture.height) return false;

  ComPtr<ID3D12Resource> upload;
  auto heap = Heap(D3D12_HEAP_TYPE_UPLOAD);
  auto bd = BufferDesc(total);
  if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&upload))))
    return false;
  uint8_t* mapped = nullptr;
  if (FAILED(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) return false;
  const uint8_t* src = static_cast<const uint8_t*>(rows);
  for (UINT y = 0; y < numRows; ++y)
    memcpy(mapped + fp.Offset + static_cast<size_t>(y) * fp.Footprint.RowPitch,
           src + static_cast<size_t>(y) * rowBytes, rowBytes);
  upload->Unmap(0, nullptr);

  if (!Begin()) return false;
  Transition(texture, D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = texture.resource.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = upload.Get();
  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source.PlacedFootprint = fp;
  list_->CopyTextureRegion(&dst, 0, 0, 0, &source, nullptr);
  Transition(texture, finalState);
  return EndAndWait(timeoutMs);
}

bool GpuContext::UploadInputs(const uint8_t* rgba, size_t rgbaBytes, uint32_t timeoutMs) {
  const size_t pixels = static_cast<size_t>(color_.width) * color_.height;
  if (!rgba || rgbaBytes != pixels * 4) return false;
  const ULONGLONG deadline = GetTickCount64() + timeoutMs;
  auto remaining = [&]() -> uint32_t {
    ULONGLONG now = GetTickCount64();
    if (now >= deadline) return 0;
    return static_cast<uint32_t>(deadline - now);
  };
  std::vector<uint16_t> color(pixels * 4);
  for (size_t i = 0; i < pixels * 4; ++i)
    color[i] = XMConvertFloatToHalf(rgba[i] / 255.0f);
  std::vector<float> depth(pixels, 0.5f);
  std::vector<uint16_t> motion(pixels * 2, XMConvertFloatToHalf(0.0f));
  uint32_t wait = remaining();
  if (!wait || !UploadTexture(color_, color.data(), static_cast<size_t>(color_.width) * 8,
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, wait)) return false;
  wait = remaining();
  if (!wait || !UploadTexture(depth_, depth.data(), static_cast<size_t>(depth_.width) * 4,
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, wait)) return false;
  wait = remaining();
  if (!wait || !UploadTexture(motion_, motion.data(), static_cast<size_t>(motion_.width) * 4,
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, wait)) return false;
  wait = remaining();
  if (!wait || !Begin()) return false;
  Transition(output_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  return EndAndWait(wait);
}

bool GpuContext::CopyColorToOutput(uint32_t timeoutMs) {
  if (!color_.resource || !output_.resource || !Begin()) return false;
  Transition(color_, D3D12_RESOURCE_STATE_COPY_SOURCE);
  Transition(output_, D3D12_RESOURCE_STATE_COPY_DEST);
  list_->CopyResource(output_.resource.Get(), color_.resource.Get());
  Transition(color_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  Transition(output_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  return EndAndWait(timeoutMs);
}

bool GpuContext::ReadOutput(std::vector<uint8_t>& rgba, uint32_t timeoutMs) {
  D3D12_RESOURCE_DESC td = output_.resource->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
  UINT numRows = 0;
  UINT64 rowSize = 0, total = 0;
  device_->GetCopyableFootprints(&td, 0, 1, 0, &fp, &numRows, &rowSize, &total);
  ComPtr<ID3D12Resource> readback;
  auto heap = Heap(D3D12_HEAP_TYPE_READBACK);
  auto bd = BufferDesc(total);
  if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&readback))))
    return false;
  if (!Begin()) return false;
  Transition(output_, D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = readback.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = fp;
  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = output_.resource.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  Transition(output_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!EndAndWait(timeoutMs)) return false;

  const uint8_t* mapped = nullptr;
  if (FAILED(readback->Map(0, nullptr, reinterpret_cast<void**>(const_cast<uint8_t**>(&mapped)))))
    return false;
  const size_t pixels = static_cast<size_t>(output_.width) * output_.height;
  rgba.resize(pixels * 4);
  for (uint32_t y = 0; y < output_.height; ++y) {
    const uint16_t* row = reinterpret_cast<const uint16_t*>(mapped + fp.Offset +
        static_cast<size_t>(y) * fp.Footprint.RowPitch);
    for (uint32_t x = 0; x < output_.width * 4; ++x) {
      float v = XMConvertHalfToFloat(row[x]);
      v = std::max(0.0f, std::min(1.0f, v));
      rgba[(static_cast<size_t>(y) * output_.width * 4) + x] =
          static_cast<uint8_t>(v * 255.0f + 0.5f);
    }
  }
  readback->Unmap(0, nullptr);
  return true;
}
