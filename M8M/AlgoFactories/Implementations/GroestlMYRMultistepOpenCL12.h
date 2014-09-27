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


#define GROESTLMYR_DEBUGBUFFER_SIZE 0 // (1024 * 1024) // 0 to disable


struct GroestlMYRMultiStep_Options {
    asizei linearIntensity;

	GroestlMYRMultiStep_Options() : linearIntensity(10) { }
	auint HashCount() const { return 256 * linearIntensity; }
	asizei OptimisticNonceCountMatch() const { const asizei estimate(HashCount() / (64 * 1024));    return 32u > estimate? 32u : estimate; }
	bool operator==(const GroestlMYRMultiStep_Options &other) const { return linearIntensity == other.linearIntensity; }
};

/*! Resource structures are used by the AbstractCLAlgoImplementation and the AbstractThreadedMiner as placeholders, handy structs to group stuff together.
They DO NOT own the resources pointed - outer code does and provides proper RAII semantics. Those must be PODs. */
struct GroestlMYRMultiStep_Resources {
	cl_command_queue commandq;
	cl_mem wuData, dispatchData; //!< constant buffer input to various stages, input
	cl_mem grsOutput;
	cl_mem nonces; //!< out from last pass, begins with found nonce count, then all nonces follow, output
	std::array<cl_program, 5> program;
	std::array<cl_kernel, 5> kernel;
#if GROESTLMYR_DEBUGBUFFER_SIZE
	cl_mem debugBUFFER;
#endif
	cl_event resultsTransferred;
	cl_uint *mappedNonceBuff;
	void Clear() { memset(this, 0, sizeof(*this));  } // POD makes it easy!
	GroestlMYRMultiStep_Resources() { Clear(); }
	// ~QubitMultiStep_Resources(); // no. Outer code manages ownership.

	void Free() {
		if(resultsTransferred) clReleaseEvent(resultsTransferred);
		for(asizei i = 0; i < kernel.size(); i++) if(kernel[i]) clReleaseKernel(kernel[i]);
		for(asizei i = 0; i < program.size(); i++) if(program[i]) clReleaseProgram(program[i]);
		if(grsOutput) clReleaseMemObject(grsOutput);
		if(nonces) clReleaseMemObject(nonces);
		if(dispatchData) clReleaseMemObject(dispatchData);
		if(wuData) clReleaseMemObject(wuData);
		if(commandq) clReleaseCommandQueue(commandq);
	}
};


class GroestlMYRMultistepOpenCL12 : public AbstractCLAlgoImplementation<5, GroestlMYRMultiStep_Options, GroestlMYRMultiStep_Resources> {
public:
	const bool PROFILING_ENABLED;
	const static auint MAX_CONCURRENCY;

	GroestlMYRMultistepOpenCL12(bool profiling, OpenCL12Wrapper::ErrorFunc f = nullptr) : PROFILING_ENABLED(profiling), AbstractCLAlgoImplementation("fiveSteps", "1", f) {	}
	bool Dispatch(asizei setIndex, asizei slotIndex) {
		throw std::exception("not implemented yet!");
	}
	void HashHeader(std::array<aubyte, 32> &hash, const std::array<aubyte, 128> &header, asizei setIndex, asizei slotIndex) {
		throw std::exception("not implemented yet!");
	}

	~GroestlMYRMultistepOpenCL12() { }

	void GetSettingsInfo(SettingsInfo &out, asizei setting) const {
		throw std::exception("not implemented yet!");
	}

private:
	static bool LoadFile(std::unique_ptr<char[]> &source, asizei &len, const std::string &name) {
		throw std::exception("not implemented yet!");
	}
	void ProbeProgramBuildInfo(cl_program program, cl_device_id device, const std::string &defines) const {
		throw std::exception("not implemented yet!");
	}
	static std::string GetBuildOptions(const GroestlMYRMultiStep_Options &opt, auint index) {
		throw std::exception("not implemented yet!");
	}
	static std::string GetSourceFileName(const GroestlMYRMultiStep_Options &opt, auint index) {
		throw std::exception("not implemented yet!");
	}
	static std::string GetKernelName(const GroestlMYRMultiStep_Options &opt, auint index) {
		throw std::exception("not implemented yet!");
	}
	void Parse(GroestlMYRMultiStep_Options &opt, const rapidjson::Value &params) {
		throw std::exception("not implemented yet!");
	}
	asizei ChooseSettings(const OpenCL12Wrapper::Platform &plat, const OpenCL12Wrapper::Device &dev, RejectReasonFunc callback) {
		throw std::exception("not implemented yet!");
	}
	void BuildDeviceResources(GroestlMYRMultiStep_Resources &target, cl_context ctx, cl_device_id dev, const GroestlMYRMultiStep_Options &opt) {
		throw std::exception("not implemented yet!");
	}
	GroestlMYRMultistepOpenCL12* NewDerived() const { return new GroestlMYRMultistepOpenCL12(PROFILING_ENABLED, errorCallback); }
	void PutMidstate(aubyte *dst, const stratum::AbstractWorkUnit &wu) const { } //! \todo I don't think groestl-myr has a midstate, check out.
	std::string CustomVersioningStrings() const {
		throw std::exception("not implemented yet!");
	}
};
