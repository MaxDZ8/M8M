/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/Settings.h"
#include "../Common/ArenDataTypes.h"
#include "../Common/Stratum/WorkUnit.h"
#include "MinerInterface.h"

template<typename MiningProcessorsProvider>
class AbstractAlgoImplementation {
    const char *name;
public:
	AbstractAlgoImplementation(const char *caseInsensitiveName) : name(caseInsensitiveName) { }
    
	bool AreYou(const char *name) const { return !_stricmp(name, this->name); }
	std::string GetName() const { return std::string(name); }
    
    //! This isn't really just a validate call, it really prepares internal data and associates an index
    //! to this task. The task however is not immediately ready to run.
	virtual asizei ValidateSettings(MiningProcessorsProvider &procs, const std::vector<Settings::ImplParam> &params) = 0;

	//! Clear all the resources associated with this implementation.
	virtual void Clear(MiningProcessorsProvider &api) = 0;
    
	virtual bool Ready(asizei resourceSlot) const = 0; //!< if this returns false, then Prepare will get called
    
	virtual void Prepare(asizei resourceSlot) = 0;
    
	virtual bool CanTakeInput(asizei resourceSlot) const = 0;
    
	//! Called only if CanTakeInput returned true, returns the amount of hashes which will be tested
    //! given the current settings for resource slot specified.
    virtual auint BeginProcessing(asizei resourceSlot, const stratum::WorkUnit &wu, auint prevHashes) = 0;
    
	/*! If the implementation has results ready to be poured out for a given configuration identified by index then
	this set of resulting nonces will have benn associated to a job id and a nonce2, taken by the corresponding BeginProcessing
	parameters. Note this is almost a MinerInterface::Nonces structure, except algorithm implementations do not track owner pools.
	The outer code can easily reconstruct this information using the resource slot index. */
	virtual bool ResultsAvailable(stratum::WorkUnit &wu, std::vector<auint> &results, asizei resourceSlot) = 0;
    
	/*! If the algorithm has something to do, this will return 0 and add nothing to the list.
	Otherwise it returns the amount of wait handles added to the list.
	There's no guarantee on the order, the outer code will just sleep until at least one event in its pool (which could
	encompass different algorithms) will be marked as ready. Therefore, it could be possible for an algorithm to be
	spuriously woken up.
	\note This function must be lightweight, a mere getter conceptually. All structures required to make it work must
	be generated at the end of the last pass. */
    virtual auint GetWaitEvents(std::vector<typename MiningProcessorsProvider::WaitEvent> &list, asizei resourceSlot) const = 0;

    //! Try to advance all the internal tasks one step. Implementations should try to not cause the
    //! thread to block.
    virtual void Dispatch() = 0;

	/*! CPU-side verification of a given header, already populated with the nonce to test.
	Hashing is performed coherently with the specified parameter set so for example a scrypt-N algorithm can discriminate
	between hashes for N=1024, N=2048 etc. This unfortunately mandates some replication of code, but that's it. */
	virtual void HashHeader(std::array<aubyte, 32> &hash, const std::array<aubyte, 128> &header, asizei internalIndex) = 0;
};
