#include "../engine/dlssnr2fx.h"

static nr_status (*eval_fn)(const nr_eval_desc*) = nr_eval;
static void (*cancel_fn)(uint32_t, uint32_t) = nr_cancel;
static void (*shutdown_fn)(void) = nr_shutdown;

int engine_header_c_compile_probe(void) {
  return eval_fn != 0 && cancel_fn != 0 && shutdown_fn != 0;
}
