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


#define DEBUGBUFFER_SIZE 0 // (1024 * 1024) // 0 to disable


struct QubitMultiStep_Options {
    asizei linearIntensity;

	QubitMultiStep_Options() : linearIntensity(10) { }
	auint HashCount() const { return 256 * linearIntensity; }
	asizei OptimisticNonceCountMatch() const { const asizei estimate(HashCount() / (64 * 1024));    return 32u > estimate? 32u : estimate; }
	bool operator==(const QubitMultiStep_Options &other) const { return linearIntensity == other.linearIntensity; }
};

/*! Resource structures are used by the AbstractCLAlgoImplementation and the AbstractThreadedMiner as placeholders, handy structs to group stuff together.
They DO NOT own the resources pointed - outer code does and provides proper RAII semantics. Those must be PODs. */
struct QubitMultiStep_Resources {
	cl_command_queue commandq;
	cl_mem wuData, dispatchData; //!< constant buffer input to various stages, input
	std::array<cl_mem, 2> io; //!< 1st step takes from wuData, dispatchData, outputs to 0. N+1 step takes from N%2, outputs to (N+1)%2
	cl_mem nonces; //!< out from last pass, begins with found nonce count, then all nonces follow, output
	std::array<cl_program, 5> program;
	std::array<cl_kernel, 5> kernel;
	cl_mem aesLUTTables, alphaTable, betaTable; //!< those could be shared across different queues... on the same device. We take it easy.
#if DEBUGBUFFER_SIZE
	cl_mem debugBUFFER;
#endif
	cl_event resultsTransferred;
	cl_uint *mappedNonceBuff;
	void Clear() { memset(this, 0, sizeof(*this));  } // POD makes it easy!
	QubitMultiStep_Resources() { Clear(); }
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


class QubitMultistepOpenCL12 : public AbstractCLAlgoImplementation<5, QubitMultiStep_Options, QubitMultiStep_Resources> {
public:
	const bool PROFILING_ENABLED;

	QubitMultistepOpenCL12(bool profiling, OpenCL12Wrapper::ErrorFunc f = nullptr) : PROFILING_ENABLED(profiling), AbstractCLAlgoImplementation("fiveSteps", "1", f) {	}
	bool Dispatch(asizei setIndex, asizei slotIndex);
	void HashHeader(std::array<aubyte, 32> &hash, const std::array<aubyte, 128> &header, asizei setIndex, asizei slotIndex);

	~QubitMultistepOpenCL12();

	void GetSettingsInfo(SettingsInfo &out, asizei setting) const;

private:
	static std::string GetBuildOptions(const QubitMultiStep_Options &opt, auint index);
	std::pair<std::string, std::string> GetSourceFileAndKernel(asizei settingIndex, auint stepIndex) const;
	void Parse(QubitMultiStep_Options &opt, const rapidjson::Value &params);
	asizei ChooseSettings(const OpenCL12Wrapper::Platform &plat, const OpenCL12Wrapper::Device &dev, RejectReasonFunc callback);
	void BuildDeviceResources(QubitMultiStep_Resources &target, cl_context ctx, cl_device_id dev, const QubitMultiStep_Options &opt, asizei confIndex);
	QubitMultistepOpenCL12* NewDerived() const { return new QubitMultistepOpenCL12(PROFILING_ENABLED, errorCallback); }
	void PutMidstate(aubyte *dst, const stratum::AbstractWorkUnit &wu) const { } //! \todo Qubit allows midstate, first iteration of luffa. Todo.
};
