/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCLAlgoImplementation.h"
#include "../../../Common/aes.h"
#include <sstream>
#include "../../KnownHardware.h"


#define NEOSCRYPT_DEBUGBUFFER_SIZE 0 // (1024 * 1024) // 0 to disable


struct NeoScryptMultiStep_Options {
    auint linearIntensity;

	NeoScryptMultiStep_Options() : linearIntensity(10) { }
	auint HashCount() const { return 64 * linearIntensity; } // not so bad really but this consumes way too much memory!
	auint OptimisticNonceCountMatch() const { const auint estimate(HashCount() / (64 * 1024));    return 32u > estimate? 32u : estimate; }
	bool operator==(const NeoScryptMultiStep_Options &other) const { return linearIntensity == other.linearIntensity; }
};

/*! Resource structures are used by the AbstractCLAlgoImplementation and the AbstractThreadedMiner as placeholders, handy structs to group stuff together.
They DO NOT own the resources pointed - outer code does and provides proper RAII semantics. Those must be PODs. */
struct NeoScryptMultiStep_Resources {
	cl_command_queue commandq;
	cl_mem wuData, dispatchData;
    cl_mem nonces; 
	cl_event resultsTransferred;
	cl_uint *mappedNonceBuff;
    // ^ typical stuff most algos have

    // Neoscrypt is an initial and final KDF with a "core loop" inside. Legacy kernels use registers for the KDF while I use memory,
    // which also allows me to keep one buffer around. They're not very explicative in naming: in general KDF involves fetching an hash (inner)
    // to another has (outer). What's important in this case is that there's an "A" which is basically a set of constants while there's "B" buffer
    // which is (initially identical to A but shorter) modified according to hashing.
    cl_mem buff_a; //!< KDF_SIZE+BLAKE_BLOCK_SIZE = 256+64 bytes per hash
    cl_mem buff_b; //!< KDF_SIZE+KEY_SIZE = 256 + 32 bytes per hash

    cl_kernel firstKDF, lastKDF;

    //! The result of the firstKDF is a 256-byte hash fetched to the Salsa-based Sequential Write. This must be kept around across the whole loop
    //! because it is fetched to loop y=1 as well but it is then stored again as the value "X" at end of loop y=1
    cl_mem xstart;
    cl_mem scratchPad; //!< 32*1024 bytes per hash, the big one

    //! Result of a processing y loop iteration consisting of X = SW(X),IR(X) by some block cipher (Salsa or Chacha).
    cl_mem x0, x1;

    // With two outer loops, I just recompile everything with flags so the compiler has maximum flexibilty in doing the right thing.
    cl_kernel salsaSW, salsaIR, chachaSW, chachaIR;

	std::array<cl_program, 6> program;

#if NEOSCRYPT_DEBUGBUFFER_SIZE
	cl_mem debugBUFFER;
#endif
	void Clear() { memset(this, 0, sizeof(*this));  } // POD makes it easy!
	NeoScryptMultiStep_Resources() { Clear(); }
	// ~NeoScryptMultiStep_Resources(); // no. Outer code manages ownership.

	void Free() {
		if(resultsTransferred) clReleaseEvent(resultsTransferred);
		if(nonces) clReleaseMemObject(nonces);
		if(wuData) clReleaseMemObject(wuData);
		if(commandq) clReleaseCommandQueue(commandq);
        
        if(buff_a) clReleaseMemObject(buff_a);
        if(buff_b) clReleaseMemObject(buff_b);
        if(xstart) clReleaseMemObject(xstart);
        if(scratchPad) clReleaseMemObject(scratchPad);
        if(x0) clReleaseMemObject(x0);
        if(x1) clReleaseMemObject(x1);

        if(salsaSW) clReleaseKernel(salsaSW);
        if(salsaIR) clReleaseKernel(salsaIR);
        if(chachaSW) clReleaseKernel(chachaSW);
        if(chachaIR) clReleaseKernel(chachaIR);
        if(firstKDF) clReleaseKernel(firstKDF);
        if(lastKDF) clReleaseKernel(lastKDF);

		for(asizei i = 0; i < program.size(); i++) if(program[i]) clReleaseProgram(program[i]);
	}
};


class NeoScryptMultistepOpenCL12 : public AbstractCLAlgoImplementation<1, NeoScryptMultiStep_Options, NeoScryptMultiStep_Resources> {
public:
	const bool PROFILING_ENABLED;

	NeoScryptMultistepOpenCL12(bool profiling, OpenCL12Wrapper::ErrorFunc f = nullptr) : PROFILING_ENABLED(profiling), AbstractCLAlgoImplementation("smooth", "1", f, true) {	}
	bool Dispatch(asizei setIndex, asizei slotIndex);
	void HashHeader(std::array<aubyte, 32> &hash, const std::array<aubyte, 128> &header, asizei setIndex, asizei slotIndex);

	~NeoScryptMultistepOpenCL12();

	void GetSettingsInfo(SettingsInfo &out, asizei setting) const;

	static const cl_uint ITERATIONS = 128;
	static const cl_uint STATE_SLICES = 4;
	static const cl_uint MIX_ROUNDS = 10;

private:
	static std::string GetBuildOptions(const NeoScryptMultiStep_Options &opt, auint index);
	std::pair<std::string, std::string> GetSourceFileAndKernel(asizei settingIndex, auint stepIndex) const;
	void Parse(NeoScryptMultiStep_Options &opt, const rapidjson::Value &params);
	asizei ChooseSettings(const OpenCL12Wrapper::Platform &plat, const OpenCL12Wrapper::Device &dev, RejectReasonFunc callback);
	void BuildDeviceResources(NeoScryptMultiStep_Resources &target, cl_context ctx, cl_device_id dev, const NeoScryptMultiStep_Options &opt, asizei confIndex);
	NeoScryptMultistepOpenCL12* NewDerived() const { return new NeoScryptMultistepOpenCL12(PROFILING_ENABLED, errorCallback); }
	void PutMidstate(aubyte *dst, const stratum::AbstractWorkUnit &wu) const { } //! \note probably not really worth it
};
