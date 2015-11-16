/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <string.h>

namespace knownHardware {
    struct Architecture {
        constexpr explicit Architecture(const char *userName, bool CPU, bool GPU)
            : presentationString(userName), cpu(CPU), gpu(GPU) { }
        constexpr const char* GetPresentationString(bool forceNonNull) const {
            return presentationString == nullptr && forceNonNull? "Unknown" : presentationString;
        }
        const bool cpu, gpu;
    private:
        const char* presentationString;
    };

    constexpr Architecture arch_unknown { nullptr, false, false }; 
    constexpr Architecture arch_gcn_1_0 { "Graphics Core Next 1.0", false, true };
    constexpr Architecture arch_gcn_1_1 { "Graphics Core Next 1.1", false, true };
    constexpr Architecture arch_gcn_1_2 { "Graphics Core Next 1.2", false, true };
    constexpr Architecture arch_gcn_1_x { "Graphics Core Next 1.x", false, true };

    static Architecture GetArch(unsigned __int32 vendorid, bool cpu, bool gpu, const std::string &chipname, const char *extensions) {
        auto matches = [&chipname](const std::initializer_list<char*> &list) {
            for(const auto &el : list) {
                if(el == chipname) return true;
            }
            return false;
        };
        auto matchExt = [extensions](const char *match) {
            const char *begin = extensions;
            const char *end = extensions + strlen(extensions);
            const asizei mlen = strlen(match);
            while(begin < end) {
                const char *limit = begin;
                while(limit < end && *limit != ' ') limit++;
                if(mlen == limit - begin) {
                    if(!strncmp(match, begin, mlen)) return true;
                }
                begin = limit + 1;
            }
            return false;
        };
        switch(vendorid) {
        case 0x00001002: // AMD
            if(gpu) { // codexl 1.8.9637.0
                if(matches({ "Capeverde", "Hainan", "Oland", "Pitcairn", "Tahiti" })) return arch_gcn_1_0;
                if(matches({ "Bonaire", "Hawaii", "Kalindi", "Mullins", "Spectre", "Spooky" })) return arch_gcn_1_1;
                if(matches({ "Carrizo", "Fiji", "Iceland", "Tonga" })) return arch_gcn_1_2;
                if(matchExt("cl_khr_int64_base_atomics")) return arch_gcn_1_x; // I've been told this is GCN-only for the time being
            }
            if(cpu) {
                // "AMD Athlon(tm) II X3 450 Processor" --> "K10-Propus".
            }
            break;
        }
        return arch_unknown;
    }
}
