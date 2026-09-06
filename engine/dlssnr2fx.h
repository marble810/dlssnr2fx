#ifndef DLSSNR2FX_H
#define DLSSNR2FX_H

#include <stdint.h>
#include <wchar.h>

#if defined(_WIN32)
#  if defined(DLSSNR2FX_EXPORTS)
#    define NR_API __declspec(dllexport)
#  else
#    define NR_API __declspec(dllimport)
#  endif
#  define NR_CALL __cdecl
#else
#  define NR_API
#  define NR_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nr_status {
  NR_OK = 0,
  NR_ERR_MISSING_RUNTIME = 1,
  NR_ERR_GPU = 2,
  NR_ERR_NGX = 3,
  NR_ERR_TIMEOUT = 4,
  NR_ERR_BUSY = 5,
  NR_ERR_SUPERSEDED = 6,
  NR_ERR_ARGS = 7,
  NR_ERR_DEAD = 8
} nr_status;

typedef struct nr_model {
  uint32_t preset;
  uint32_t style;
  uint32_t auto_mask;
  uint32_t ui_correction;
  float intensity;
  float local_structure;
  float local_tone;
  float skin;
  float global_tone;
} nr_model;

typedef enum nr_pixel_format {
  NR_PIX_RGBA8 = 0,   /* uint8 x4, 0..255. Zero-init keeps this default. */
  NR_PIX_RGBA32F = 1  /* float x4, typically 0..1; values may exceed 1 */
} nr_pixel_format;

typedef enum nr_fg_mode {
  NR_FG_AUTO = 0,          /* reset on skip/reverse/instance change */
  NR_FG_ALWAYS_RESET = 1,  /* Reset=1 every new frame (compositor default) */
  NR_FG_LOCK = 2           /* keep history except first eval / instance change */
} nr_fg_mode;

typedef enum nr_guide_source {
  NR_GUIDE_OFF = 0,      /* zeros */
  NR_GUIDE_INPUT = 1,    /* host packed in_mvec / in_depth */
  NR_GUIDE_ESTIMATE = 2  /* NVOF or Depth Anything V2 */
} nr_guide_source;

typedef enum nr_nvof_quality {
  NR_NVOF_SLOW = 0,
  NR_NVOF_MEDIUM = 1,
  NR_NVOF_FAST = 2
} nr_nvof_quality;

typedef struct nr_eval_desc {
  int32_t width;
  int32_t height;
  int32_t adapter; /* adapter < 0 selects the default high-performance adapter */
  uint32_t instance_id;
  uint32_t request_id;
  uint32_t timeout_ms;
  nr_model model;
  float detail;
  float color;
  int32_t hdr;
  const wchar_t* runtime_dir; /* absolute directory containing nvngx_dlssnr.dll */
  nr_pixel_format format;
  int64_t frame_id;   /* host time ticks; identical id = same source frame */
  int64_t frame_step; /* expected +delta to the next forward frame; <=0 = any change resets */
  int32_t reset;      /* 1 = force DLSSNR.Reset; 0 = engine decides */
  nr_fg_mode fg_mode; /* 0 Auto, 1 Always Reset, 2 Lock */
  int32_t fg_coalesce; /* 1 = reuse last output for the same frame key; 0 = off */
  const void* in_rgba; /* packed RGBA8 or RGBA32F, width*height texels */
  const void* in_prev_rgba; /* T-1, same format/size as in_rgba; null = no previous frame */
  void* out_rgba;
  nr_guide_source mvec_source; /* 0 Off, 1 Input, 2 Estimate (NVOF) */
  nr_guide_source depth_source; /* 0 Off, 1 Input, 2 Estimate (DAV2) */
  nr_nvof_quality nvof_quality; /* Slow/Medium/Fast */
  int32_t nvof_hint; /* 1 = temporal hints */
  int32_t dav2_interval; /* infer every N frames; <=0 = 4 */
  float dav2_ema; /* 0..1 history blend; <0 = 0.7 */
  const float* in_mvec; /* optional width*height*2, current-to-previous pixels */
  const float* in_depth; /* optional width*height, relative 0..1 (near high if inverted) */
  float* out_mvec; /* optional width*height*2, the vectors actually consumed */
  float* out_depth; /* optional width*height, the depth actually consumed */
  int32_t maps_only; /* 1 = fill out_mvec/out_depth and skip NGX */
} nr_eval_desc;

NR_API nr_status NR_CALL nr_eval(const nr_eval_desc* desc);
NR_API void NR_CALL nr_cancel(uint32_t instance_id, uint32_t request_id);
/* Bounded best-effort release. Never call nr_shutdown from DllMain. */
NR_API void NR_CALL nr_shutdown(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DLSSNR2FX_H */
