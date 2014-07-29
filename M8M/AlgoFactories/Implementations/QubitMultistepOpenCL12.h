/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCLAlgoImplementation.h"
#include <fstream>
#include <iostream>
#include "../../../Common/aes.h"


#define DEBUGBUFFER_SIZE 0 // (1024 * 1024) // 0 to disable


struct QubitMultiStep_Options {
    asizei linearIntensity;
    asizei dispatchCount;

	QubitMultiStep_Options() : linearIntensity(10), dispatchCount(4) { }
	auint Concurrency() const { return 256 * linearIntensity; }
	asizei HashesPerDispatch() const { return Concurrency(); }
	asizei HashesPerPass() const { return HashesPerDispatch() * dispatchCount; }
	asizei OptimisticNonceCountMatch() const { const asizei estimate(HashesPerPass() / (64 * 1024));    return 32u > estimate? 32u : estimate; }
};

struct QubitMultiStep_Resources {
	cl_context ctx;
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
	QubitMultiStep_Resources();
	~QubitMultiStep_Resources();
	QubitMultiStep_Resources(QubitMultiStep_Resources &&other);
	QubitMultiStep_Resources& operator=(QubitMultiStep_Resources &&other);
private:
	QubitMultiStep_Resources& operator=(QubitMultiStep_Resources &other); // missing link no thanks!
};


class QubitMultistepOpenCL12 : public AbstractCLAlgoImplementation<5, QubitMultiStep_Options, QubitMultiStep_Resources> {
public:
	const bool PROFILING_ENABLED;
	const static auint MAX_CONCURRENCY;
	OpenCL12Wrapper::ErrorFunc errorCallback;

	QubitMultistepOpenCL12(bool profiling, OpenCL12Wrapper::ErrorFunc f = nullptr) : PROFILING_ENABLED(profiling), AbstractCLAlgoImplementation("multistep"), errorCallback(f) {	}
    asizei ValidateSettings(OpenCL12Wrapper &cl, const std::vector<Settings::ImplParam> &params);
	bool Ready(asizei resourceSlot) const;
	void Prepare(asizei resourceSlot);
	void Dispatch();
	void HashHeader(std::array<aubyte, 32> &hash, const std::array<aubyte, 128> &header, asizei internalIndex);

private:
	typedef QubitMultiStep_Options Options;
	typedef QubitMultiStep_Resources Resources;

	static bool LoadFile(std::unique_ptr<char[]> &source, asizei &len, const std::string &name);
	void ProbeProgramBuildInfo(cl_program program, cl_device_id device, const std::string &defines) const;
	static std::string GetBuildOptions(const Options &opt, auint index);
	static std::string GetSourceFileName(const Options &opt, auint index);
	static std::string GetKernelName(const Options &opt, auint index);
};
