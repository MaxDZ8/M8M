/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCLAlgoImplementation.h"
#include <iostream>
#include "../../../Common/aes.h"
#include <sstream>
#include "../../KnownHardware.h"


#define FRESH_DEBUGBUFFER_SIZE 0 //(1024 * 1024) // 0 to disable

/*! \todo Identical to Qubit, minus slightly different compilation.
Refer to QubitMultistepOpenCL12, I should really find a way to make chained hashing work in a more generic way.
Example:
Qubit is-a ChainedHash({ std::make_pair(string("Luffa_1W.cl"), string("Luffa_1way")),
	                     std::make_pair(string("CubeHash_2W.cl"), string("CubeHash_2way")),
	                     std::make_pair(string("SHAvite3_1W.cl"), string("SHAvite3_1way")),
	                     std::make_pair(string("SIMD_16W.cl"), string("SIMD_16way")),
	                     std::make_pair(string("Echo_8W.cl"), string("Echo_8way"))
                    });
With C++ initialization lists it would be super cool... albeit I'm not quite sold.
Anyway, Fresh would be
Fresh is-a ChainedHash(shavite, simd, shavite, simd, echo)

The first kernel gets HEAD_OF_CHAINED_HASHING defined.

By the way, kernels should really be a different thing, more data driven. They could be json files such as

kernelList.json 
{
    [
        {
            file: "Luffa_1W.cl",    // link to an external file, it's convenient to have them standalone
            kernel: "Luffa_1way",
            head: true,             // true if kernel supports fetching from a 80-byte block --> HEAD_OF_CHAINED_HASHING
            chainable: true,        // true if kernel supports incremental fetching of an hash --> ...?
        }
    ]
}
*/

struct FreshMultiStep_Options {
    asizei linearIntensity;

	FreshMultiStep_Options() : linearIntensity(10) { }
	auint HashCount() const { return 256 * linearIntensity; }
	asizei OptimisticNonceCountMatch() const { const asizei estimate(HashCount() / (64 * 1024));    return 32u > estimate? 32u : estimate; }
	bool operator==(const FreshMultiStep_Options &other) const { return linearIntensity == other.linearIntensity; }
};

struct FreshMultiStep_Resources {
	cl_command_queue commandq;
	cl_mem wuData, dispatchData; //!< constant buffer input to various stages, input
	std::array<cl_mem, 2> io; //!< 1st step takes from wuData, dispatchData, outputs to 0. N+1 step takes from N%2, outputs to (N+1)%2
	cl_mem nonces; //!< out from last pass, begins with found nonce count, then all nonces follow, output
	std::array<cl_program, 5> program;
	std::array<cl_kernel, 5> kernel;
	cl_mem aesLUTTables, alphaTable, betaTable; //!< those could be shared across different queues... on the same device. We take it easy.
#if FRESH_DEBUGBUFFER_SIZE
	cl_mem debugBUFFER;
#endif
	cl_event resultsTransferred;
	cl_uint *mappedNonceBuff;
	void Clear() { memset(this, 0, sizeof(*this));  } // POD makes it easy!
	FreshMultiStep_Resources() { Clear(); }
	// ~QubitMultiStep_Resources(); // no. Outer code manages ownership.

	void Free() {
		if(resultsTransferred) clReleaseEvent(resultsTransferred);
		for(asizei i = 0; i < kernel.size(); i++) if(kernel[i]) clReleaseKernel(kernel[i]);
		for(asizei i = 0; i < program.size(); i++) if(program[i]) clReleaseProgram(program[i]);
		for(asizei i = 0; i < io.size(); i++) if(io[i]) clReleaseMemObject(io[i]);
		if(aesLUTTables) clReleaseMemObject(aesLUTTables);
		if(alphaTable) clReleaseMemObject(alphaTable);
		if(betaTable) clReleaseMemObject(betaTable);
		if(nonces) clReleaseMemObject(nonces);
		if(dispatchData) clReleaseMemObject(dispatchData);
		if(wuData) clReleaseMemObject(wuData);
		if(commandq) clReleaseCommandQueue(commandq);
	}
};


class FreshMultistepOpenCL12 : public AbstractCLAlgoImplementation<1, FreshMultiStep_Options, FreshMultiStep_Resources> {
public:
	const bool PROFILING_ENABLED;

	FreshMultistepOpenCL12(bool profiling, OpenCL12Wrapper::ErrorFunc f = nullptr) : PROFILING_ENABLED(profiling), AbstractCLAlgoImplementation("warm", "1", f) {	}
	bool Dispatch(asizei setIndex, asizei slotIndex);
	void HashHeader(std::array<aubyte, 32> &hash, const std::array<aubyte, 128> &header, asizei setIndex, asizei slotIndex);

	~FreshMultistepOpenCL12();

	void GetSettingsInfo(SettingsInfo &out, asizei setting) const;

private:
	static std::string GetBuildOptions(const FreshMultiStep_Options &opt, auint index);
	std::pair<std::string, std::string> GetSourceFileAndKernel(asizei settingIndex, auint stepIndex) const;
	void Parse(FreshMultiStep_Options &opt, const rapidjson::Value &params);
	asizei ChooseSettings(const OpenCL12Wrapper::Platform &plat, const OpenCL12Wrapper::Device &dev, RejectReasonFunc callback);
	void BuildDeviceResources(FreshMultiStep_Resources &target, cl_context ctx, cl_device_id dev, const FreshMultiStep_Options &opt, asizei confIndex);
	FreshMultistepOpenCL12* NewDerived() const { return new FreshMultistepOpenCL12(PROFILING_ENABLED, errorCallback); }
	void PutMidstate(aubyte *dst, const stratum::AbstractWorkUnit &wu) const { } //! \todo Fresh allows midstate, first iteration of ShaVite3
};
