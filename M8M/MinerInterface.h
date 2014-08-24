/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/AREN/ArenDataTypes.h"
#include <string>
#include "../Common/Stratum/AbstractWorkUnit.h"
#include "../Common/Settings.h"
#include "../Common/AbstractWorkSource.h"
#include <chrono>
#include "AlgoImplementationInterface.h"


/*! A miner is a set of processing elements/devices running an algorithm. A miner does not really own those devices: it manages them and uses them for compute.
The algorithm and implementation to be used is to be selected immediately after creation by using SetCurrentAlgo(). After an algorithm has been selected, you
can propose configurations for the specific algorithm implementation by using AddConfig(). Once all the configs have been uploaded, call GenResources()
to assign each processor to its most appropriate config and allocate the resources appropriately according to configs. 

Multiple processors can share a configuration. It is not necessary to use all processors. All the processors used are basically "homogeneous": they all run
the same algo and if they use the same settings then they basically run the same thing, only with different resources. */
class MinerInterface {
public:
	virtual ~MinerInterface() { }

	/*! Returns a list of all the supported algorithms.
	- Spaces are not significant.
	- Algo names have no spaces.
	- Algo names are separated by comma ',' and possibly space.
	Each algorithm can be enabled or not but submitting a working unit using an algorithm which is not enabled is a fatal error. */
	virtual std::string GetAlgos() const = 0;

	/*! For each algorithm, returns a list of the supported implementations. This string has the same format as GetAlgos().
	Unsupported algos get an empty string. */
	virtual std::string GetImplementations(const char *algo) const = 0;

	/*! Select the algorithm to mine. Only a single algo can be mined at a given moment in time (or no algo at all).
	It is supposed to be called only once after creation. After this has returned true, call SetAvailableConfigs().
	Assume this is the only valid call after construction! In particular, implementations are free to crash if AddSettings
	is unable to find a proper structure to store setup.
	\todo In the future, have this reboot everything
	\note In current implementations, it is acceptable to only allow this to become invalid after first successful call.
	\return false if the algorithm cannot be found. */
	virtual bool SetCurrentAlgo(const char *algo, const char *implementation) = 0;

	/*! Used by external code to understand if a work-unit can be dispatched there. If no algo is being run, returns nullptr. */
	virtual const char* GetMiningAlgo() const = 0;

	/*! Returns false if no algorithm currently selected. Otherwise, updates the two strings by probing the algorithm. */
	virtual bool GetMiningAlgoImpInfo(std::string &name, std::string &version) const = 0;

	/*! Given an algorithm implementation, it might be useful to define a different set of configs to run the algorithm.
	The typical example is the primary video card for an interactive computer, which will have to stay at higher degrees of interactivity
	to not impact user's productivity. A dedicated card by contrast will likely be more powerful and need higher amounts of workload...
	even only to reach the same level of interactivity. Call this to produce new algorithm configurations.

	This function can be called at any time, even if the miner is already crunching data. The newly added configs don't need to be dispatched
	to the processing loop: this only happens when GenResources() is called.

	\note Implementations must expect the same config could be repeated multiple times and they must be able to consider re-declarations as nop. */
	virtual void AddSettings(const std::vector<Settings::ImplParam> &params) = 0;

	/*! Once all configurations have been poured in, call this to match each device to a proper config and to later create all the resources
	necessary to computation. No processing will take place until this is called. */
	virtual void Start() = 0;
	
	/*! Signals a new set of data to be mangled. This interface does not mandate how to dispatch this new data to the processors.
	Implementations are also allowed to be heavily pipelined and keep working on previous work units from the same owner even after this is called.
	Implementations required to support multiple data sources, albeit they have no requirement to serve the different data sources equally,
	it is valid to rotate them every once in a while, or not rotate them at all. 

	The only requirement by the calling code is to use algorithms compatible with the current mining process. 
	\note It is calling code requirement to ensure WorkUnit::owner to have the same algorithm as the one being mined. This implies that if the code
	dispatched wrong, it will get wrong nonces. The miner itself is not concerned with this. 
	\note Initially, everything here was passed by value. This is no more the case to allow for flexible WU rolling. Takes ownership of the passed WU
	and the external code should never use it again. */
	virtual void Mangle(const AbstractWorkSource &owner, std::unique_ptr<stratum::AbstractWorkUnit> &wu) = 0;

	/*! Returns the pool owner passed at last Mangle call, which could be nullptr. It might (or might not) be the pool being currently mined.
	Implementations are encouraged in keeping the value returned here coherent with the mining thread as well but it is not required.
	For the purpose of coherent implementations, this function is not const. */
	virtual const AbstractWorkSource* GetCurrentPool() = 0;

	typedef std::chrono::high_resolution_clock HPCLK;
	typedef std::chrono::time_point<std::chrono::high_resolution_clock> HPTP;
	
	/*! The miner produces one of this structures whatever it can find at least one nonce as returned by the algorithm. After pulling out the nonce however:
	1) it might be stale, coming out from an old job
	2) it might be wrong (very rare with current kernels)
	A structure of this kind is produced and made available by SharesFound even when the list of found nonces is empty after the above filtering.
	Number of shares output by algo = this->nonces.size() + this->stale + this->bad. */
	struct Nonces {
		const AbstractWorkSource *owner;
		std::string job;
		auint nonce2;
		std::vector<auint> nonces; //!< how many good nonces have been found with this nonce2, can be empty if all nonces were stale or discarded!
		asizei deviceIndex; //!< a number, uniquely identifying the device used to produce those numbers, suitable to be used with a bounded vector/array.
		std::chrono::microseconds scanPeriod; //!< time took from algo start to pulling out the values
		asizei lastNonceScanAmount; //!< how many hashes tested in this algo iteration
		asizei stale; //!< number of nonces discarded because they were not matching the job id anymore (they likely get all discarded and nonces.size() will be 0)
		asizei bad; //!< how many nonces, after verification turned out to be wrong and thus not included in the nonce list.
		Nonces() : stale(0), bad(0), owner(nullptr) { }
	};
	/*! Call this every once in a while to get newly found nonces. Because implementations are not required to filter stale results, external code
	should ensure the nonces are still in the current job. Implementations might produce spurious nonces in case this call fails (such as because
	the push_back operation fails) so the outer code should filter that.
	In practice, if you call this function every time you can there should be no complications.
	Returns true if nonces where found, regardless they were later discarded or not so you can collect information. */
	virtual bool SharesFound(std::vector<Nonces> &results) = 0;

	/*! This function serves various purposes. It can be used to count the amount of available devices and if they are being used or not.
	Implementations must return false if device >= available devices (regardless they are being used or not).
	If a device can be resolved, config must be 0 if not used.
	Otherwise, it is 1+config slot used.
	So basicall, if(GetDeviceConfig(index, i) && index) device is using configuration index-1. */
	virtual bool GetDeviceConfig(asizei &config, asizei device) const = 0;

	/*! M8M tries to be an EDUCATIONAL miner. This means it tries to be a very informative, open, "communicational" miner.
	This is perhaps where I pay most of the price: give me a device and I'll tell you why it was not mapped to a configuration in a way hopefully
	everybody understands. If the device is mapped, then the returned array is empty. */
	virtual std::vector<std::string> GetBadConfigReasons(asizei device) const = 0;

	//!< \todo Getters are starting to pile up. It would be nice to refactor them out to another interface.
	virtual const AlgoImplementationInterface* GetAI(const char *family, const char *impl) const = 0;


};
