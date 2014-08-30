/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/Settings.h"
#include "../Common/AREN/ArenDataTypes.h"
#include "../Common/Stratum/AbstractWorkUnit.h"
#include <functional>

//! Not really an AlgoImplementation at this point, mostly bookeeping information.
class AlgoImplementationInterface {
public:
	virtual ~AlgoImplementationInterface() { }
    
	virtual bool AreYou(const char *name) const = 0;
	virtual std::string GetName() const = 0;
	virtual aulong GetVersioningHash() const = 0;
    
    /*! Algorithm implementations parse the set of parameters to internal structures, only them can have understanding of the parameters to use
	and how to sanity-check them. Implementations are encouraged in keeping the settings unique.
	\sa MinerInterface::AddConfig */
	virtual void AddSettings(const std::vector<Settings::ImplParam> &params) = 0;

	/*! Returns the amount of settings currently stored in this algo implementation. Ideally that would be the number of successful AddSettings calls
	minus the number of settings which were aliased. */
	virtual asizei GetNumSettings() const = 0;

	struct SettingsInfo { //!< Used to return data about a specific setting, somewhat "static data".
		asizei hashCount;
		struct BufferInfo {
			std::string addressSpace; // "device", "host"...
			std::string presentation; //  name of the parameter, or more involved discussion
			std::vector<std::string> accessType; // const, in, out
			asizei footprint; //!< how many bytes the corresponding parameter is taking. The user does not see that. In bytes.
		};
		std::vector<BufferInfo> resource;
		//! Some parameters are copied, some are moved, names are unique, qualifiers likely not.
		void Push(const std::string &addrSpace, const std::vector<std::string> &access, std::string &desc, asizei bytes) {
			resource.push_back(BufferInfo());
			resource.back().addressSpace = addrSpace;
			resource.back().presentation = std::move(desc);
			resource.back().accessType = access;
			resource.back().footprint = bytes;
		}
		void Push(const std::string &addrSpace, const std::vector<std::string> &access, const char *desc, asizei bytes) {
			Push(addrSpace, access, std::string(desc), bytes);
		}
	};
	virtual void GetSettingsInfo(SettingsInfo &out, asizei setting) const = 0;
};
