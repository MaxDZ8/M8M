/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractAlgoImplementation.h"
#include "../OpenCL12Wrapper.h"

/*! Abstract, multi-step OpenCL algorithm implementation. Step 0 is always data load. Step [1..1+NUM_STEPS] are the various sub-algorithms
you are going to dispatch. Step 1+NUM_STEPS is assumed to also map the result buffers and initiate the results ready event. */
template<auint NUM_STEPS, typename Options, typename Resources>
class AbstractCLAlgoImplementation : public AbstractAlgoImplementation<OpenCL12Wrapper> {
public:
	bool ResultsAvailable(stratum::WorkUnit &wu, std::vector<auint> &results, auint instance) {	
		AlgoInstance &use(algoInstances[instance]);
		if(use.step <= NUM_STEPS) return false;
		//! \todo Consider multiple results iterations here!
		cl_int status;
		cl_int error = clGetEventInfo(use.res.resultsTransferred, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof(status), &status, NULL);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clGetEventInfo while polling for CL event result map.";
		if(status < 0) throw std::string("OpenCL event error status: ") + std::to_string(error) + " returned by clGetEventInfo while polling for CL event result map.";
		if(status != CL_COMPLETE) return false;
		wu = use.wu;
		if(use.res.mappedNonceBuff[0] > use.options.OptimisticNonceCountMatch())
			throw std::string("Found ") + std::to_string(use.res.mappedNonceBuff[0]) + " nonces, but only " + std::to_string(use.options.OptimisticNonceCountMatch()) + " could be stored.";
		using std::min;
		const asizei nonceCount = min(asizei(use.res.mappedNonceBuff[0]), use.options.OptimisticNonceCountMatch());
		results.reserve(results.size() + nonceCount);
		for(asizei cp = 0; cp < nonceCount; cp++)
			results.push_back(use.res.mappedNonceBuff[1 + cp]);
		clReleaseEvent(use.res.resultsTransferred);
		use.res.resultsTransferred = 0;
		error = clEnqueueUnmapMemObject(use.res.commandq, use.res.nonces, use.res.mappedNonceBuff, 0, NULL, NULL);
		if(error) throw std::string("OpenCL error ") + std::to_string(error) + " on result nonce buffer unmap.";
		use.res.mappedNonceBuff = nullptr;
		use.step = 0; // no more things to do here
		return true;
	}
	
    auint BeginProcessing(asizei resourceSlot, const stratum::WorkUnit &wu, auint prevHashes) {
		AlgoInstance &use(algoInstances[resourceSlot]);
		const aubyte *blob = reinterpret_cast<const aubyte*>(wu.target.data());
		const aulong target = *reinterpret_cast<const aulong*>(blob + 24);
		FillWorkUnitData(use.res.commandq, use.res.wuData, wu);
		FillDispatchData(use.res.commandq, use.res.dispatchData, use.options.Concurrency(), target, use.options.OptimisticNonceCountMatch());
		use.wu = wu;
		use.nonceBase = prevHashes;
		use.step = 1;
		if(use.res.resultsTransferred) clReleaseEvent(use.res.resultsTransferred);
		use.res.resultsTransferred = 0;
		cl_uint zero = 0;
		clEnqueueWriteBuffer(use.res.commandq, use.res.nonces, true, 0, sizeof(cl_uint), &zero, 0, NULL, NULL);
		return use.options.HashesPerPass();
	}
	bool CanTakeInput(asizei resourceSlot) const {
		return resourceSlot < algoInstances.size() && algoInstances[resourceSlot].step == 0;
	}
	auint GetWaitEvents(std::vector<cl_event> &list, asizei resourceSlot) const {
		cl_event ev = algoInstances[resourceSlot].res.resultsTransferred;
		if(ev) list.push_back(ev);
		return ev != 0? 1 : 0;
	}
	void Clear(OpenCL12Wrapper &api) { algoInstances.clear(); }

protected:
	AbstractCLAlgoImplementation(const char *name) : AbstractAlgoImplementation(name) { }

	/*! The DispatchData buffer is a small blob of data available to all kernels implementing CL. It's nothing really related to hashing
	but more like supporting the computation. */
	static void FillDispatchData(cl_command_queue cq, cl_mem hwbuff, cl_uint concurrency, cl_ulong diffTarget, cl_uint maxNonces) {
		cl_uint buffer[5];
		buffer[0] = concurrency;
		buffer[1] = static_cast<cl_uint>(diffTarget >> 32);
		buffer[2] = static_cast<cl_uint>(diffTarget);
		buffer[3] = maxNonces;
		buffer[4] = 0;
		cl_int error = clEnqueueWriteBuffer(cq, hwbuff, CL_TRUE, 0, sizeof(buffer), buffer, 0, NULL, NULL);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueWriteBuffer while writing to dispatchData buffer.";
	}

	//! Really does two things: 1) produces the target difficulty to be used by FillDispatchData and 2) fills the buffer 'wuData'. Note some kernels
	//! don't use the additional midstate... maybe I'll have to work on that.
	static void FillWorkUnitData(cl_command_queue cq, cl_mem hwbuff, const stratum::WorkUnit &wu) {
		cl_uint buffer[32];
		aubyte *dst = reinterpret_cast<aubyte*>(buffer);
		asizei rem = 128;
		memcpy_s(dst, rem, wu.header.data(), 80);	dst += 80;	rem -= 80;
		memcpy_s(dst, rem, wu.midstate.data(), 32);	dst += 32;	rem -= 32;
		//memcpy_s(dst, rem, &target, 4);				dst += 4;	rem -= 4;
		cl_int error = clEnqueueWriteBuffer(cq, hwbuff, true, 0, sizeof(buffer), buffer, 0, NULL, NULL);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueWriteBuffer while writing to wuData buffer.";
		//! \note SPH kernels in legacy miners flip the header from LE to BE. They then flip again in the first chained hashing step.
	}

	struct AlgoInstance {
		Options options;
		Resources res;
		asizei step;
		auint nonceBase;
		stratum::WorkUnit wu; // job id, nonce2 information
		AlgoInstance() : step(0) { }
		AlgoInstance(AlgoInstance &&other) { options = other.options;  res = std::move(other.res);  step = other.step;  nonceBase = other.nonceBase;  wu = other.wu; }
		AlgoInstance& operator=(AlgoInstance &&other) { options = other.options;  res = std::move(other.res);  step = other.step;  nonceBase = other.nonceBase;  wu = other.wu;  return *this; }
	private:
		AlgoInstance(const AlgoInstance &other); // missing, forbidden
		AlgoInstance& operator=(const AlgoInstance &other); // missing, forbidden
	};
	std::vector<AlgoInstance> algoInstances;
};
