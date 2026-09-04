#include "AEConfig.h"
#include "AE_EffectVers.h"
#include "dlssnr_aex_version.h"

#ifndef AE_OS_WIN
	#include "AE_General.r"
#endif
resource 'PiPL' (16000) {
	{	/* array properties */
		/* [1] */
		Kind {
			AEEffect
		},
		/* [2] */
		Name {
			"DLSS Neural Rendering"
		},
		/* [3] */
		Category {
			"Filter"
		},
		/* [4] */
#ifdef AE_OS_WIN
    #if defined(AE_PROC_INTELx64)
		CodeWin64X86 {"EffectMain"},
    #elif defined(AE_PROC_ARM64)
		CodeWinARM64 {"EffectMain"},
    #endif
#elif defined(AE_OS_MAC)
		CodeMacIntel64 {"EffectMain"},
		CodeMacARM64 {"EffectMain"},
#endif
		/* [6] */
		AE_PiPL_Version {
			2,
			0
		},
		/* [7] */
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		/* [8] */
		AE_Effect_Version {	
			AEX_PIPL_VERSION	/* 1.0 */
		},
		/* [9] */
		AE_Effect_Info_Flags {
			0
		},
		/* [10] */
		AE_Effect_Global_OutFlags {
			0x2000000  /* DEEP_COLOR_AWARE only; NOT PIX_INDEPENDENT (full-frame pipe) */
		},
		AE_Effect_Global_OutFlags_2 {
			0  /* Classic PF only; FLOAT_COLOR_AWARE requires Smart Render */
		},
		/* [11] */
		AE_Effect_Match_Name {
			"ADBE DLSS Neural Rendering"
		},
		/* [12] */
		AE_Reserved_Info {
			0
		}
	}
};