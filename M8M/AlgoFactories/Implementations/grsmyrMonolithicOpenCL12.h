/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCLAlgoImplementation.h"
#include <fstream>
#include <iostream>
#include "../../../Common/aes.h"
#include <sstream>
#include "../../KnownHardware.h"


#define GROESTLMYR_DEBUGBUFFER_SIZE 0 // 0 to disable


struct GRSMYRMonolithic_Options {
    auint linearIntensity;

	GRSMYRMonolithic_Options() : linearIntensity(10) { }
	// Compared to Qubit, grsmyr looks slower (say 25%) BUT I implement it monolithically so to maintain a similar degree of interactivity
	// I have to go half.
	auint HashCount() const { return 512 * linearIntensity; }
	auint OptimisticNonceCountMatch() const { const auint estimate(HashCount() / (64 * 1024));    return 32u > estimate? 32u : estimate; }
	bool operator==(const GRSMYRMonolithic_Options &other) const { return linearIntensity == other.linearIntensity; }
};

/*! Resource structures are used by the AbstractCLAlgoImplementation and the AbstractThreadedMiner as placeholders, handy structs to group stuff together.
They DO NOT own the resources pointed - outer code does and provides proper RAII semantics. Those must be PODs.
\todo a lot of similarities to Qubit multistep! I should make this explicit somehow.*/
struct GRSMYRMonolithic_Resources {
	cl_command_queue commandq;
	cl_mem wuData, dispatchData; //!< constant buffer input to various stages, input
	cl_mem nonces; //!< out from last pass, begins with found nonce count, then all nonces follow, output
	cl_mem roundCount; //!< This is a stupid mini-buffer used to prevent loop unrolling in a portable way
	cl_program program;
	cl_kernel kernel;
#if GROESTLMYR_DEBUGBUFFER_SIZE
	cl_mem debugBUFFER;
#endif
	cl_event resultsTransferred;
	cl_uint *mappedNonceBuff;
	void Clear() { memset(this, 0, sizeof(*this));  } // POD makes it easy!
	GRSMYRMonolithic_Resources() { Clear(); }
	// ~QubitMultiStep_Resources(); // no. Outer code manages ownership.

	void Free() {
		if(resultsTransferred) clReleaseEvent(resultsTransferred);
		if(kernel) clReleaseKernel(kernel);
		if(program) clReleaseProgram(program);
		if(roundCount) clReleaseMemObject(roundCount);
		if(nonces) clReleaseMemObject(nonces);
		if(dispatchData) clReleaseMemObject(dispatchData);
		if(wuData) clReleaseMemObject(wuData);
		if(commandq) clReleaseCommandQueue(commandq);
	}
};


class GRSMYRMonolithicOpenCL12 : public AbstractCLAlgoImplementation<1, GRSMYRMonolithic_Options, GRSMYRMonolithic_Resources> {
public:
	const bool PROFILING_ENABLED;

	GRSMYRMonolithicOpenCL12(bool profiling, OpenCL12Wrapper::ErrorFunc f = nullptr) : PROFILING_ENABLED(profiling), AbstractCLAlgoImplementation("monolithic", "1", f, false) {	}
	bool Dispatch(asizei setIndex, asizei slotIndex);
	void HashHeader(std::array<aubyte, 32> &hash, const std::array<aubyte, 128> &header, asizei setIndex, asizei slotIndex) {
		throw std::exception("not implemented yet!");
	}

	~GRSMYRMonolithicOpenCL12() { }

	void GetSettingsInfo(SettingsInfo &out, asizei setting) const;

private:
	std::string GetBuildOptions(const GRSMYRMonolithic_Options &opt, auint index) const;
    std::pair<std::string, std::string> GetSourceFileAndKernel(asizei settingIndex, auint stepIndex) const;
	void Parse(GRSMYRMonolithic_Options &opt, const rapidjson::Value &params);
	asizei ChooseSettings(const OpenCL12Wrapper::Platform &plat, const OpenCL12Wrapper::Device &dev, RejectReasonFunc callback);
	void BuildDeviceResources(GRSMYRMonolithic_Resources &add, cl_context ctx, cl_device_id use, const GRSMYRMonolithic_Options &opt, asizei confIndex);
	GRSMYRMonolithicOpenCL12* NewDerived() const { return new GRSMYRMonolithicOpenCL12(PROFILING_ENABLED, errorCallback); }
	void PutMidstate(aubyte *dst, const stratum::AbstractWorkUnit &wu) const { } //! \todo I don't think groestl-myr has a midstate, check out.
};
