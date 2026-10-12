#pragma once

#define AEX_MAJOR_VERSION 1
#define AEX_MINOR_VERSION 0
#define AEX_BUG_VERSION 0
#define AEX_STAGE_VERSION PF_Stage_DEVELOP
#define AEX_BUILD_VERSION 2

// Packed PF_VERSION value consumed by PiPLtool. Keep this checked against the
// SDK's PF_VERSION macro via the static_assert in dlssnr_aex.cpp.
#define AEX_PIPL_VERSION 524290
