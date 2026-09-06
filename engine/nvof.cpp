#include "nvof.h"

#include "nvof_decl.h"

#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
void Rgba8ToBgra(const uint8_t* rgba, uint8_t* bgra, size_t pixels) {
  for (size_t i = 0; i < pixels; ++i) {
    bgra[i * 4 + 0] = rgba[i * 4 + 2];
    bgra[i * 4 + 1] = rgba[i * 4 + 1];
    bgra[i * 4 + 2] = rgba[i * 4 + 0];
    bgra[i * 4 + 3] = rgba[i * 4 + 3];
  }
}

void Rgba32fToBgra(const float* rgba, uint8_t* bgra, size_t pixels) {
  for (size_t i = 0; i < pixels; ++i) {
    auto q = [](float v) {
      v = std::max(0.f, std::min(1.f, v));
      return static_cast<uint8_t>(v * 255.f + 0.5f);
    };
    bgra[i * 4 + 0] = q(rgba[i * 4 + 2]);
    bgra[i * 4 + 1] = q(rgba[i * 4 + 1]);
    bgra[i * 4 + 2] = q(rgba[i * 4 + 0]);
    bgra[i * 4 + 3] = q(rgba[i * 4 + 3]);
  }
}

NV_OF_PERF_LEVEL PerfFromQuality(int quality) {
  if (quality <= 0) return NV_OF_PERF_LEVEL_SLOW;
  if (quality >= 2) return NV_OF_PERF_LEVEL_FAST;
  return NV_OF_PERF_LEVEL_MEDIUM;
}

bool CreateTex2D(ID3D11Device* device, DXGI_FORMAT format, UINT w, UINT h, UINT bind,
                 ID3D11Texture2D** out) {
  D3D11_TEXTURE2D_DESC d{};
  d.Width = w;
  d.Height = h;
  d.MipLevels = 1;
  d.ArraySize = 1;
  d.Format = format;
  d.SampleDesc.Count = 1;
  d.Usage = D3D11_USAGE_DEFAULT;
  d.BindFlags = bind;
  return SUCCEEDED(device->CreateTexture2D(&d, nullptr, out));
}
}  // namespace

struct NvofEstimator::Impl {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  HMODULE module = nullptr;
  NV_OF_D3D11_API_FUNCTION_LIST api{};
  NvOFHandle session = nullptr;
  ComPtr<ID3D11Texture2D> input[2];
  ComPtr<ID3D11Texture2D> flow;
  ComPtr<ID3D11Texture2D> flowStaging;
  NvOFGPUBufferHandle inputH[2]{};
  NvOFGPUBufferHandle flowH = nullptr;
  LUID luid{};
  int width = 0;
  int height = 0;
  int quality = -1;
  uint32_t grid = 4;

  ~Impl() { Destroy(); }

  void Unregister(NvOFGPUBufferHandle& h) {
    if (h && api.nvOFUnregisterResourceD3D11) api.nvOFUnregisterResourceD3D11(h);
    h = nullptr;
  }

  void Destroy() {
    Unregister(inputH[0]);
    Unregister(inputH[1]);
    Unregister(flowH);
    if (session && api.nvOFDestroy) api.nvOFDestroy(session);
    session = nullptr;
    api = {};
    input[0].Reset();
    input[1].Reset();
    flow.Reset();
    flowStaging.Reset();
    context.Reset();
    device.Reset();
    if (module) {
      FreeLibrary(module);
      module = nullptr;
    }
    width = height = 0;
    quality = -1;
  }

  bool Init(LUID adapterLuid, int w, int h, int q) {
    Destroy();
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
    ComPtr<IDXGIAdapter1> chosen;
    for (UINT i = 0;; ++i) {
      ComPtr<IDXGIAdapter1> a;
      if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
      DXGI_ADAPTER_DESC1 desc{};
      if (FAILED(a->GetDesc1(&desc))) continue;
      if (desc.AdapterLuid.LowPart == adapterLuid.LowPart &&
          desc.AdapterLuid.HighPart == adapterLuid.HighPart) {
        chosen = a;
        break;
      }
    }
    if (!chosen) return false;
    UINT flags = 0;
    if (FAILED(D3D11CreateDevice(chosen.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, nullptr, 0,
                                 D3D11_SDK_VERSION, &device, nullptr, &context)))
      return false;

    module = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return false;
    auto createApi = reinterpret_cast<PFNNvOFAPICreateInstanceD3D11>(
        GetProcAddress(module, "NvOFAPICreateInstanceD3D11"));
    auto getMax = reinterpret_cast<PFNNvOFGetMaxSupportedApiVersion>(
        GetProcAddress(module, "NvOFGetMaxSupportedApiVersion"));
    if (!createApi) return false;
    uint32_t version = NR_NVOF_API_VERSION;
    if (getMax) {
      uint32_t maxVer = 0;
      if (getMax(&maxVer) == NV_OF_SUCCESS && maxVer >= NR_NVOF_API_VERSION)
        version = NR_NVOF_API_VERSION;
    }
    if (createApi(version, &api) != NV_OF_SUCCESS || !api.nvCreateOpticalFlowD3D11 ||
        !api.nvOFInit || !api.nvOFExecute || !api.nvOFRegisterResourceD3D11)
      return false;
    if (api.nvCreateOpticalFlowD3D11(device.Get(), context.Get(), &session) != NV_OF_SUCCESS ||
        !session)
      return false;

    grid = 4;
    if (api.nvOFGetCaps) {
      uint32_t count = 0;
      if (api.nvOFGetCaps(session, NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES, nullptr, &count) ==
              NV_OF_SUCCESS &&
          count > 0 && count < 16) {
        std::vector<uint32_t> sizes(count);
        if (api.nvOFGetCaps(session, NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES, sizes.data(),
                            &count) == NV_OF_SUCCESS) {
          for (uint32_t candidate : {4u, 2u, 1u}) {
            if (std::find(sizes.begin(), sizes.end(), candidate) != sizes.end()) {
              grid = candidate;
              break;
            }
          }
        }
      }
    }

    NV_OF_INIT_PARAMS init{};
    init.width = static_cast<uint32_t>(w);
    init.height = static_cast<uint32_t>(h);
    init.outGridSize = static_cast<NV_OF_OUTPUT_VECTOR_GRID_SIZE>(grid);
    init.mode = NV_OF_MODE_OPTICALFLOW;
    init.perfLevel = PerfFromQuality(q);
    if (api.nvOFInit(session, &init) != NV_OF_SUCCESS) return false;

    if (!CreateTex2D(device.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, w, h, D3D11_BIND_SHADER_RESOURCE,
                     &input[0]) ||
        !CreateTex2D(device.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, w, h, D3D11_BIND_SHADER_RESOURCE,
                     &input[1]))
      return false;
    const UINT fw = (static_cast<UINT>(w) + grid - 1) / grid;
    const UINT fh = (static_cast<UINT>(h) + grid - 1) / grid;
    if (!CreateTex2D(device.Get(), DXGI_FORMAT_R16G16_SINT, fw, fh, D3D11_BIND_SHADER_RESOURCE,
                     &flow))
      return false;
    D3D11_TEXTURE2D_DESC sd{};
    sd.Width = fw;
    sd.Height = fh;
    sd.MipLevels = 1;
    sd.ArraySize = 1;
    sd.Format = DXGI_FORMAT_R16G16_SINT;
    sd.SampleDesc.Count = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&sd, nullptr, &flowStaging))) return false;
    if (api.nvOFRegisterResourceD3D11(session, input[0].Get(), &inputH[0]) != NV_OF_SUCCESS ||
        api.nvOFRegisterResourceD3D11(session, input[1].Get(), &inputH[1]) != NV_OF_SUCCESS ||
        api.nvOFRegisterResourceD3D11(session, flow.Get(), &flowH) != NV_OF_SUCCESS)
      return false;

    luid = adapterLuid;
    width = w;
    height = h;
    quality = q;
    return true;
  }
};

NvofEstimator::~NvofEstimator() { delete impl_; }

void NvofEstimator::Reset() {
  if (impl_) impl_->Destroy();
}

bool NvofEstimator::Ensure(LUID adapter, int width, int height, int quality) {
  if (width <= 0 || height <= 0) return false;
  if (!impl_) impl_ = new Impl();
  if (impl_->session && impl_->width == width && impl_->height == height &&
      impl_->quality == quality && impl_->luid.LowPart == adapter.LowPart &&
      impl_->luid.HighPart == adapter.HighPart)
    return true;
  return impl_->Init(adapter, width, height, quality);
}

bool NvofEstimator::Execute(const uint8_t* currentBgra, const uint8_t* previousBgra,
                            bool temporalHint, std::vector<float>& xy) {
  const UINT row = static_cast<UINT>(impl_->width) * 4;
  impl_->context->UpdateSubresource(impl_->input[0].Get(), 0, nullptr, previousBgra, row, 0);
  impl_->context->UpdateSubresource(impl_->input[1].Get(), 0, nullptr, currentBgra, row, 0);
  NV_OF_EXECUTE_INPUT_PARAMS in{};
  in.inputFrame = impl_->inputH[1];
  in.referenceFrame = impl_->inputH[0];
  in.disableTemporalHints = temporalHint ? NV_OF_FALSE : NV_OF_TRUE;
  NV_OF_EXECUTE_OUTPUT_PARAMS out{};
  out.outputBuffer = impl_->flowH;
  if (impl_->api.nvOFExecute(impl_->session, &in, &out) != NV_OF_SUCCESS) return false;
  impl_->context->CopyResource(impl_->flowStaging.Get(), impl_->flow.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(impl_->context->Map(impl_->flowStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
    return false;
  const int gw = (impl_->width + static_cast<int>(impl_->grid) - 1) / static_cast<int>(impl_->grid);
  const int gh = (impl_->height + static_cast<int>(impl_->grid) - 1) / static_cast<int>(impl_->grid);
  std::vector<float> grid(static_cast<size_t>(gw) * gh * 2);
  for (int y = 0; y < gh; ++y) {
    const auto* rowPtr = reinterpret_cast<const int16_t*>(
        static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch);
    for (int x = 0; x < gw; ++x) {
      grid[(static_cast<size_t>(y) * gw + x) * 2] = rowPtr[x * 2] / 32.f;
      grid[(static_cast<size_t>(y) * gw + x) * 2 + 1] = rowPtr[x * 2 + 1] / 32.f;
    }
  }
  impl_->context->Unmap(impl_->flowStaging.Get(), 0);

  xy.resize(static_cast<size_t>(impl_->width) * impl_->height * 2);
  const float scale = static_cast<float>(impl_->grid);
  for (int y = 0; y < impl_->height; ++y) {
    const float gy = (gh == 1) ? 0.f : (y + 0.5f) / scale - 0.5f;
    const int y0 = std::max(0, std::min(gh - 1, (int)std::floor(gy)));
    const int y1 = std::max(0, std::min(gh - 1, y0 + 1));
    const float ty = gy - y0;
    for (int x = 0; x < impl_->width; ++x) {
      const float gx = (gw == 1) ? 0.f : (x + 0.5f) / scale - 0.5f;
      const int x0 = std::max(0, std::min(gw - 1, (int)std::floor(gx)));
      const int x1 = std::max(0, std::min(gw - 1, x0 + 1));
      const float tx = gx - x0;
      auto at = [&](int ix, int iy) {
        return grid.data() + (static_cast<size_t>(iy) * gw + ix) * 2;
      };
      const float* a = at(x0, y0);
      const float* b = at(x1, y0);
      const float* c = at(x0, y1);
      const float* d = at(x1, y1);
      const size_t i = (static_cast<size_t>(y) * impl_->width + x) * 2;
      xy[i] = (a[0] * (1 - tx) + b[0] * tx) * (1 - ty) + (c[0] * (1 - tx) + d[0] * tx) * ty;
      xy[i + 1] = (a[1] * (1 - tx) + b[1] * tx) * (1 - ty) + (c[1] * (1 - tx) + d[1] * tx) * ty;
    }
  }
  return true;
}

bool NvofEstimator::EstimateRgba8(LUID adapter, const uint8_t* current, const uint8_t* previous,
                                  int width, int height, int quality, bool temporalHint,
                                  std::vector<float>& xy) {
  if (!current || !previous || !Ensure(adapter, width, height, quality)) {
    xy.clear();
    return false;
  }
  const size_t pixels = static_cast<size_t>(width) * height;
  std::vector<uint8_t> cur(pixels * 4), prev(pixels * 4);
  Rgba8ToBgra(current, cur.data(), pixels);
  Rgba8ToBgra(previous, prev.data(), pixels);
  if (!Execute(cur.data(), prev.data(), temporalHint, xy)) {
    Reset();
    xy.clear();
    return false;
  }
  return true;
}

bool NvofEstimator::EstimateRgba32f(LUID adapter, const float* current, const float* previous,
                                    int width, int height, int quality, bool temporalHint,
                                    std::vector<float>& xy) {
  if (!current || !previous || !Ensure(adapter, width, height, quality)) {
    xy.clear();
    return false;
  }
  const size_t pixels = static_cast<size_t>(width) * height;
  std::vector<uint8_t> cur(pixels * 4), prev(pixels * 4);
  Rgba32fToBgra(current, cur.data(), pixels);
  Rgba32fToBgra(previous, prev.data(), pixels);
  if (!Execute(cur.data(), prev.data(), temporalHint, xy)) {
    Reset();
    xy.clear();
    return false;
  }
  return true;
}
