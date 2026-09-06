#pragma once

// Minimal NVOF D3D11 ABI. Types match NVIDIA Optical Flow SDK 2.x
// (nvOpticalFlowCommon.h, BSD-3). Function list order matches nvofapi64.dll
// NvOFAPICreateInstanceD3D11. We do not link an import library.

#include <d3d11.h>
#include <stdint.h>

#define NVOFAPI __stdcall

typedef struct NvOFHandle_st* NvOFHandle;
typedef struct NvOFGPUBufferHandle_st* NvOFGPUBufferHandle;
typedef struct NVOFPrivDataHandle_st* NvOFPrivDataHandle;

typedef enum NV_OF_STATUS {
  NV_OF_SUCCESS = 0,
  NV_OF_ERR_OF_NOT_AVAILABLE = 1,
  NV_OF_ERR_UNSUPPORTED_DEVICE = 2,
  NV_OF_ERR_DEVICE_DOES_NOT_EXIST = 3,
  NV_OF_ERR_INVALID_PTR = 4,
  NV_OF_ERR_INVALID_PARAM = 5,
  NV_OF_ERR_INVALID_CALL = 6,
  NV_OF_ERR_INVALID_VERSION = 7,
  NV_OF_ERR_OUT_OF_MEMORY = 8,
  NV_OF_ERR_NOT_INITIALIZED = 9,
  NV_OF_ERR_UNSUPPORTED_FEATURE = 10,
  NV_OF_ERR_GENERIC = 11
} NV_OF_STATUS;

typedef enum NV_OF_BOOL { NV_OF_FALSE = 0, NV_OF_TRUE = 1 } NV_OF_BOOL;

typedef enum NV_OF_CAPS {
  NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES = 0,
  NV_OF_CAPS_SUPPORTED_HINT_GRID_SIZES = 1,
  NV_OF_CAPS_SUPPORT_HINT_WITH_OF_MODE = 2,
  NV_OF_CAPS_SUPPORT_HINT_WITH_ST_MODE = 3,
  NV_OF_CAPS_WIDTH_MIN = 4,
  NV_OF_CAPS_HEIGHT_MIN = 5,
  NV_OF_CAPS_WIDTH_MAX = 6,
  NV_OF_CAPS_HEIGHT_MAX = 7,
  NV_OF_CAPS_SUPPORT_ROI = 8,
  NV_OF_CAPS_SUPPORT_ROI_MAX_NUM = 9
} NV_OF_CAPS;

typedef enum NV_OF_PERF_LEVEL {
  NV_OF_PERF_LEVEL_UNDEFINED = 0,
  NV_OF_PERF_LEVEL_SLOW = 5,
  NV_OF_PERF_LEVEL_MEDIUM = 10,
  NV_OF_PERF_LEVEL_FAST = 20
} NV_OF_PERF_LEVEL;

typedef enum NV_OF_OUTPUT_VECTOR_GRID_SIZE {
  NV_OF_OUTPUT_VECTOR_GRID_SIZE_UNDEFINED = 0,
  NV_OF_OUTPUT_VECTOR_GRID_SIZE_1 = 1,
  NV_OF_OUTPUT_VECTOR_GRID_SIZE_2 = 2,
  NV_OF_OUTPUT_VECTOR_GRID_SIZE_4 = 4
} NV_OF_OUTPUT_VECTOR_GRID_SIZE;

typedef enum NV_OF_HINT_VECTOR_GRID_SIZE {
  NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED = 0
} NV_OF_HINT_VECTOR_GRID_SIZE;

typedef enum NV_OF_MODE {
  NV_OF_MODE_UNDEFINED = 0,
  NV_OF_MODE_OPTICALFLOW = 1,
  NV_OF_MODE_STEREODISPARITY = 2
} NV_OF_MODE;

typedef enum NV_OF_BUFFER_USAGE {
  NV_OF_BUFFER_USAGE_UNDEFINED = 0,
  NV_OF_BUFFER_USAGE_INPUT = 1,
  NV_OF_BUFFER_USAGE_OUTPUT = 2,
  NV_OF_BUFFER_USAGE_HINT = 3,
  NV_OF_BUFFER_USAGE_COST = 4
} NV_OF_BUFFER_USAGE;

typedef enum NV_OF_BUFFER_FORMAT {
  NV_OF_BUFFER_FORMAT_UNDEFINED = 0,
  NV_OF_BUFFER_FORMAT_GRAYSCALE8 = 1,
  NV_OF_BUFFER_FORMAT_NV12 = 2,
  NV_OF_BUFFER_FORMAT_ABGR8 = 3
} NV_OF_BUFFER_FORMAT;

typedef enum NV_OF_STEREO_DISPARITY_RANGE {
  NV_OF_STEREO_DISPARITY_RANGE_UNDEFINED = 0
} NV_OF_STEREO_DISPARITY_RANGE;

typedef struct NV_OF_INIT_PARAMS {
  uint32_t width;
  uint32_t height;
  NV_OF_OUTPUT_VECTOR_GRID_SIZE outGridSize;
  NV_OF_HINT_VECTOR_GRID_SIZE hintGridSize;
  NV_OF_MODE mode;
  NV_OF_PERF_LEVEL perfLevel;
  NV_OF_BOOL enableExternalHints;
  NV_OF_BOOL enableOutputCost;
  NvOFPrivDataHandle hPrivData;
  NV_OF_STEREO_DISPARITY_RANGE disparityRange;
  NV_OF_BOOL enableRoi;
} NV_OF_INIT_PARAMS;

typedef struct NV_OF_EXECUTE_INPUT_PARAMS {
  NvOFGPUBufferHandle inputFrame;
  NvOFGPUBufferHandle referenceFrame;
  NvOFGPUBufferHandle externalHints;
  NV_OF_BOOL disableTemporalHints;
  uint32_t padding;
  NvOFPrivDataHandle hPrivData;
  uint32_t padding2;
  uint32_t numRois;
  void* roiData;
} NV_OF_EXECUTE_INPUT_PARAMS;

typedef struct NV_OF_EXECUTE_OUTPUT_PARAMS {
  NvOFGPUBufferHandle outputBuffer;
  NvOFGPUBufferHandle outputCostBuffer;
  NvOFPrivDataHandle hPrivData;
} NV_OF_EXECUTE_OUTPUT_PARAMS;

typedef NV_OF_STATUS(NVOFAPI* PFNNVOFINIT)(NvOFHandle, const NV_OF_INIT_PARAMS*);
typedef NV_OF_STATUS(NVOFAPI* PFNNVOFEXECUTE)(NvOFHandle, const NV_OF_EXECUTE_INPUT_PARAMS*,
                                              NV_OF_EXECUTE_OUTPUT_PARAMS*);
typedef NV_OF_STATUS(NVOFAPI* PFNNVOFDESTROY)(NvOFHandle);
typedef NV_OF_STATUS(NVOFAPI* PFNNVOFGETLASTERROR)(NvOFHandle, char*, uint32_t*);
typedef NV_OF_STATUS(NVOFAPI* PFNNVOFGETCAPS)(NvOFHandle, NV_OF_CAPS, uint32_t*, uint32_t*);
typedef NV_OF_STATUS(NVOFAPI* PFNNvCreateOpticalFlowD3D11)(ID3D11Device*, ID3D11DeviceContext*,
                                                           NvOFHandle*);
typedef NV_OF_STATUS(NVOFAPI* PFNNvOFGetSurfaceFormatCountD3D11)(NvOFHandle, NV_OF_BUFFER_USAGE,
                                                                 NV_OF_MODE, uint32_t*);
typedef NV_OF_STATUS(NVOFAPI* PFNNvOFGetSurfaceFormatD3D11)(NvOFHandle, NV_OF_BUFFER_USAGE,
                                                           NV_OF_MODE, DXGI_FORMAT*);
typedef NV_OF_STATUS(NVOFAPI* PFNNvOFRegisterResourceD3D11)(NvOFHandle, ID3D11Resource*,
                                                            NvOFGPUBufferHandle*);
typedef NV_OF_STATUS(NVOFAPI* PFNNvOFUnregisterResourceD3D11)(NvOFGPUBufferHandle);

typedef struct NV_OF_D3D11_API_FUNCTION_LIST {
  PFNNvCreateOpticalFlowD3D11 nvCreateOpticalFlowD3D11;
  PFNNVOFINIT nvOFInit;
  PFNNvOFGetSurfaceFormatCountD3D11 nvOFGetSurfaceFormatCountD3D11;
  PFNNvOFGetSurfaceFormatD3D11 nvOFGetSurfaceFormatD3D11;
  PFNNvOFRegisterResourceD3D11 nvOFRegisterResourceD3D11;
  PFNNvOFUnregisterResourceD3D11 nvOFUnregisterResourceD3D11;
  PFNNVOFEXECUTE nvOFExecute;
  PFNNVOFDESTROY nvOFDestroy;
  PFNNVOFGETLASTERROR nvOFGetLastError;
  PFNNVOFGETCAPS nvOFGetCaps;
} NV_OF_D3D11_API_FUNCTION_LIST;

typedef NV_OF_STATUS(NVOFAPI* PFNNvOFAPICreateInstanceD3D11)(uint32_t, NV_OF_D3D11_API_FUNCTION_LIST*);
typedef NV_OF_STATUS(NVOFAPI* PFNNvOFGetMaxSupportedApiVersion)(uint32_t*);

#define NR_NVOF_API_VERSION ((uint16_t)((2u << 4) | 0u))
