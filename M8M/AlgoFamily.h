/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractAlgoImplementation.h"
#include <vector>

template<typename MiningProcessorsProvider>
class AlgoFamily {
public:
    const char *name; //!< good to have this public as this is often initialized from persistent ctable
	AlgoFamily(const char *caseInsensitiveName) : name(caseInsensitiveName) { }
    bool AreYou(const char *name) const { return !_stricmp(name, this->name); }
	std::string GetName() const { return std::string(name); }
	//! weak, non-owned pointers
    std::vector<AbstractAlgoImplementation<MiningProcessorsProvider>*> implementations;
	void Clear(MiningProcessorsProvider &api) {
		for(asizei loop = 0; loop < implementations.size(); loop++) implementations[loop]->Clear(api);
	}
};
