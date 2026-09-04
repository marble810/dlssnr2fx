#pragma once

#include "../engine/dlssnr2fx.h"

namespace engine_client {

nr_status Eval(const nr_eval_desc& desc);
void Cancel(uint32_t instanceId, uint32_t requestId);
// Calls nr_shutdown before unloading the client module. Never call from DllMain.
void Shutdown();
const char* StatusName(nr_status status);

}  // namespace engine_client
