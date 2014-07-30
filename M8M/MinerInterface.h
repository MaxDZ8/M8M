/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/ArenDataTypes.h"
#include <string>
#include "../Common/Stratum/WorkUnit.h"
#include "../Common/Settings.h"
#include "../Common/AbstractWorkSource.h"
#include <chrono>


/*! Initially, this was supposed to be a generic way to manage computing devices.
However as I added OpenCL to the list of supported APIs I figured out there was quite a lot of code to be generalized and shared across APIs
as well as yet quite a lot of code which was not generic at all but rather mining-specific.
Therefore, in this second iteration, I want to make clear this is only a mining driver, in the sense that is logic driving the miner (thread).
It takes the legacy of the previous iteration but drops a lot of things while introducing a few more complicated structures hopefully to allow some
improved and safer method of dealing with resource creation and task dispatch. */
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

	/*! Use this function to configure the various algorithms. For each algorithm, only a single implementation can be configured.
	- NOP if the specified algorithm cannot be found.
	- Each implementation can validate the parameters in a different way and throw when they are inconsistent or above hardware capabilities.
	- Of course, it also throws on allocation errors and such.
	\returns An unique index identifying the specific algorithm and settings to be used. This is asizei(-1) for unsupported algos. */
	virtual asizei EnableAlgorithm(const char *algo, const char *implementation, const std::vector<Settings::ImplParam> &params) = 0;
	
	/*! Signals a new set of data to be mangled. This interface does not mandate how to dispatch this new data to the processors.
	Implementations are also allowed to be heavily pipelined and keep working on previous work units from the same owner even after this is called.
	Implementations required to support multiple data sources, albeit they have no requirement to serve the different data sources equally,
	it is valid to rotate them every once in a while, or not rotate them at all. 

	Notice mangling a new work unit <b>might</b> require switching to a different algorithm. The specified algorithm must have been
	specified in advance by using EnableAlgorithm(). */
	virtual void Mangle(const AbstractWorkSource &owner, const stratum::WorkUnit &wu, const auint algoSettings) = 0;

	typedef std::chrono::high_resolution_clock HPCLK;
	typedef std::chrono::time_point<std::chrono::high_resolution_clock> HPTP;
	
	struct Nonces {
		const AbstractWorkSource *owner;
		std::string job;
		auint nonce2;
		HPTP::duration lastNoncePeriod; //!< time taken to produce the last values added to this->nonces vector
		HPTP::duration averageNoncePeriod; //!< updated every time lastNoncePeriod is also updated.
		asizei lastNonceScanAmount;
		std::vector<auint> nonces;
	};
	/*! Call this every once in a while to get newly found nonces. Because implementations are not required to filter stale results, external code
	should ensure the nonces are still in the current job. Implementations might produce spurious nonces in case this call fails (such as because
	the push_back operation fails) so the outer code should filter that.
	In practice, if you call this function every time you can there should be no complications. */
	virtual bool SharesFound(std::vector<Nonces> &results) = 0;
};
