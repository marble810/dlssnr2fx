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
  const uint8_t* in_rgba;     /* width * height * 4 packed bytes */
  uint8_t* out_rgba;
} nr_eval_desc;

NR_API nr_status NR_CALL nr_eval(const nr_eval_desc* desc);
NR_API void NR_CALL nr_cancel(uint32_t instance_id, uint32_t request_id);
/* Bounded best-effort release. Never call nr_shutdown from DllMain. */
NR_API void NR_CALL nr_shutdown(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DLSSNR2FX_H */
