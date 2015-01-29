/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <string.h>

//! This ugly thing is there so I can just put a stupid hardware list here and goodbye.
struct KnownHardware {
	enum Architecture {
		arch_unknown,
		arch_gcn_first,
		arch_gcn_1_x,
		arch_gcn_1_0,
		arch_gcn_1_1,
		arch_gcn_last
	};

	enum ChipType {
		ct_cpu,
		ct_gpu
	};

	static const char* GetArchPresentationString(Architecture arch, bool forceNonNull) {
		switch(arch) {
		case arch_gcn_1_0: return "Graphics Core Next 1.0";
		case arch_gcn_1_1: return "Graphics Core Next 1.1";
		case arch_gcn_1_x: return "Graphics Core Next 1.x";
		}
		return forceNonNull? "Unknown" : nullptr;
	}

	static Architecture GetArchitecture(const unsigned __int32 vendorid, const char *chipname, ChipType ct, const char *extensions) {
#define TEST(x) if(!strcmp(#x, chipname)) return arch;
		switch(vendorid) {
		case 0x00001002: { // AMD
			if(ct == ct_gpu) {
				const Architecture arch = arch_gcn_1_0;
				TEST(Capeverde);
				TEST(Pitcairn);
				TEST(Tahiti);
			}
			if(ct == ct_gpu) {
				const Architecture arch = arch_gcn_1_1;
				TEST(Hawaii);
				TEST(Bonaire);
				TEST(Hainan);
				TEST(Curacao);
				TEST(Oland);
			}
			if(ct == ct_gpu) {
				const char *begin = extensions;
				const char *end = extensions + strlen(extensions);
				const char *match = "cl_khr_int64_base_atomics"; // I've been told this is GCN-only for the time being
				const asizei mlen = strlen(match);
				while(begin < end) {
					const char *limit = begin;
					while(limit < end && *limit != ' ') limit++;
					if(mlen == limit - begin) {
						if(!strncmp(match, begin, mlen)) return arch_gcn_1_x;
					}
					begin = limit + 1;					
				}
			}

			// "AMD Athlon(tm) II X3 450 Processor" --> "K10-Propus".
		}
		}
		return arch_unknown;
#undef TEST
	}
};