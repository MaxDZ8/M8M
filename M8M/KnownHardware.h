/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <string.h>

//! This ugly thing is there so I can just put a stupid hardware list here and goodbye.
struct KnownHardware {
	static const char* GetArchitecture(const unsigned __int32 vendorid, const char *chipname) {
#define TEST(x) if(!strcmp(#x, chipname)) return arch;
		switch(vendorid) {
		case 0x00001002: { // AMD
			{
				const char *arch = "Graphics Core Next 1.0";
				TEST(Capeverde);
				TEST(Pitcairn);
				TEST(Tahiti);
			}
			{
				const char *arch = "Graphics Core Next 1.1";
				TEST(Bonaire);
				TEST(Hainan);
				TEST(Curacao);
				TEST(Oland);
			}

			// "AMD Athlon(tm) II X3 450 Processor" --> "K10-Propus".
		}
		}
		return "";
#undef TEST
	}
};