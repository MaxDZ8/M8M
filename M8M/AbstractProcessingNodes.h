/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/ArenDataTypes.h"
#include <string>
#include "../Common/Stratum/WorkUnit.h"
#include "../Common/Settings.h"

/*! Contains all the "processors" a system features.
Each "processor" is something which can do useful work. */
class AbstractProcessingNodes {
public:
	/*! Enumeration is often necessary to deal with the correct adapter.
	This could be in fact called .Init(), it is required to always call it immediately after creation.
	However, it is not a mere init, because it involves device enumeration. This is not done in the ctor
	so there's more granularity in error checking. 
	\return Number of detected devices. */
	virtual asizei Enumerate() = 0;

	//! Returns the number of adapters enumerated. It's the same returned by Enumerate().
	virtual asizei GetAdapterCount() const = 0;

	enum AdapterProperty {
		ap_name,
		ap_vendor,
		ap_device,
		ap_processor,
		ap_revision,
		ap_misc, //!< other things, including driver-specific info
		ap_description //!< a single string containing all of the previous informations
	};

	//! Returns information about an adapter or a coalesced string of information.
	//! Invalid adapters return the empty string.
	virtual std::wstring GetInfo(asizei adapter, AdapterProperty what) const = 0;

	enum AdapterMemoryAvailability {
		ama_onboard,//!< memory dedicated on the device board
		ama_system, //!< system memory dedicated to device (at boot or by the OS in general)
		ama_shared	//!< total memory the device can "eat"
	};

	//! \return 0 for invalid adapters.
	virtual asizei GetMemorySize(asizei adapter, AdapterMemoryAvailability what) const = 0;

	//! Take a work unit and dispatch the new work to all processors.
	virtual void Mangle(const stratum::WorkUnit &wu) = 0;

	//! The processing nodes all mangle a WorkUnit using an algorithm specified there.
	virtual void UseAlgorithm(const char *algo, const char *implementation, const std::vector<Settings::ImplParam> &params) = 0;

	virtual ~AbstractProcessingNodes() { } 
	
	struct Nonces {
		std::string job;
		auint nonce2;
		std::vector<auint> nonces;
	};
	virtual bool SharesFound(std::vector<Nonces> &results) = 0;
};
