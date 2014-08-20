/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractAlgoImplementation.h"
#include "../OpenCL12Wrapper.h"
#include "../../Common/ScopedFuncCall.h"


/*! Abstract, multi-step OpenCL algorithm implementation. Step 0 is always data load. Step [1..1+NUM_STEPS] are the various sub-algorithms
you are going to dispatch. Step 1+NUM_STEPS is assumed to also map the result buffers and initiate the results ready event. */
template<auint NUM_STEPS, typename Options, typename Resources>
class AbstractCLAlgoImplementation : public AbstractAlgoImplementation<OpenCL12Wrapper> {
public:
	void AddSettings(const std::vector<Settings::ImplParam> &params) {
		Options building;
		Parse(building, params);
        auto el = std::find_if(settings.cbegin(), settings.cend(), [&building](const AlgoGroup &test) { return test.options == building; });
		if(el == settings.cend()) settings.push_back(AlgoGroup(building));
	}
	asizei GetNumSettings() const { return settings.size(); }


	void SelectSettings(OpenCL12Wrapper::ComputeNodes &procs) {
		for(auto plat = procs.begin(); plat != procs.end(); ++plat) {
			for(auto dev = plat->devices.begin(); dev != plat->devices.end(); ++dev) {
				asizei opti = SelectSettings(*plat, *dev);
				if(opti < settings.size()) {
					settings[opti].algoInstances.push_back(AlgoInstance());
					settings[opti].algoInstances.back().device = &(*dev);
				}
			}
		}
	}
	

	/*! Now that SelectSettings put our devices in the correct configurations, generating resources is super easy! */
	std::vector< std::pair<asizei, asizei> > GenResources(OpenCL12Wrapper::ComputeNodes &procs) {
		auto getPlat = [&procs](const OpenCL12Wrapper::Device *dev) -> cl_platform_id {
			auto p = std::find_if(procs.cbegin(), procs.cend(), [dev](const OpenCL12Wrapper::Platform &testp) {
				for(asizei loop = 0; loop < testp.devices.size(); loop++) {
					if(testp.devices[loop].clid == dev->clid) return true;
				}
				return false;
			});
			return p->clid;
		};
		std::vector< std::pair<asizei, asizei> > ret;
		for(asizei conf = 0; conf < settings.size(); conf++) {
			if(settings[conf].algoInstances.size() == 0) continue;
			ret.push_back(std::make_pair(conf, 0));
			for(asizei dev = 0; dev < settings[conf].algoInstances.size(); dev++) {
				AlgoInstance &inst(settings[conf].algoInstances[dev]);
				ScopedFuncCall freeRes([&inst]() { inst.res.Free(); });
				BuildDeviceResources(settings[conf].algoInstances.back().res, getPlat(inst.device), inst.device->clid, settings[conf].options);
				ret.back().second++;
				freeRes.Dont();
			}
		}
        return ret;
	}


	bool CanTakeInput(asizei optIndex, asizei instIndex) const {
        return settings[optIndex].algoInstances[instIndex].step == 0;
	}


    auint BeginProcessing(asizei optIndex, asizei instIndex, const stratum::WorkUnit &wu, auint prevHashes) {
        Options &options(settings[optIndex].options);
		AlgoInstance &use(settings[optIndex].algoInstances[instIndex]);
		const aubyte *blob = reinterpret_cast<const aubyte*>(wu.target.data());
		const aulong target = *reinterpret_cast<const aulong*>(blob + 24);
		FillWorkUnitData(use.res.commandq, use.res.wuData, wu);
		FillDispatchData(use.res.commandq, use.res.dispatchData, options.Concurrency(), target, options.OptimisticNonceCountMatch());
		use.wu = wu;
		use.nonceBase = prevHashes;
		use.step = 1;
		if(use.res.resultsTransferred) clReleaseEvent(use.res.resultsTransferred);
		use.res.resultsTransferred = 0;
		cl_uint zero = 0;
		clEnqueueWriteBuffer(use.res.commandq, use.res.nonces, true, 0, sizeof(cl_uint), &zero, 0, NULL, NULL);
		return options.HashesPerPass();
	}


	bool ResultsAvailable(stratum::WorkUnit &wu, std::vector<auint> &results, asizei optIndex, asizei instIndex) {
		Options &options(settings[optIndex].options);
		AlgoInstance &use(settings[optIndex].algoInstances[instIndex]);
		if(use.step <= NUM_STEPS) return false;
		//! \todo pipelined stuff here
		cl_int status;
		cl_int error = clGetEventInfo(use.res.resultsTransferred, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof(status), &status, NULL);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clGetEventInfo while polling for CL event result map.";
		if(status < 0) throw std::string("OpenCL event error status: ") + std::to_string(error) + " returned by clGetEventInfo while polling for CL event result map.";
		if(status != CL_COMPLETE) return false;
		wu = use.wu;
		if(use.res.mappedNonceBuff[0] > options.OptimisticNonceCountMatch())
			throw std::string("Found ") + std::to_string(use.res.mappedNonceBuff[0]) + " nonces, but only " + std::to_string(options.OptimisticNonceCountMatch()) + " could be stored.";
		using std::min;
		const asizei nonceCount = min(asizei(use.res.mappedNonceBuff[0]), options.OptimisticNonceCountMatch());
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


	auint GetWaitEvents(std::vector<cl_event> &list, asizei optIndex, asizei instIndex) const {
		cl_event ev = settings[optIndex].algoInstances[instIndex].res.resultsTransferred;
		if(ev) list.push_back(ev);
		return ev != 0? 1 : 0;
	}


	void Clear(OpenCL12Wrapper &api) { 
        for(auto el = settings.begin(); el != settings.end(); ++el) {
            el->algoInstances.clear();
		}
	}

	void MakeResourcelessCopy(std::unique_ptr<AbstractAlgoImplementation> &yours) const {
		std::unique_ptr< AbstractCLAlgoImplementation<NUM_STEPS, Options, Resources> > copy(NewDerived());
		copy->settings.resize(settings.size());
		for(asizei loop = 0; loop < settings.size(); loop++) {
			copy->settings[loop].options = settings[loop].options;
			copy->settings[loop].algoInstances.resize(settings[loop].algoInstances.size());
			for(asizei inst = 0; inst < copy->settings[loop].algoInstances.size(); inst++) {
				copy->settings[loop].algoInstances[inst].device = settings[loop].algoInstances[inst].device;
			}

		}
		yours = std::move(copy);
	}
	

	asizei GetDeviceUsedConfig(const typename OpenCL12Wrapper::Device &dev) const {
		for(asizei conf = 0; conf < settings.size(); conf++) {
			for(asizei inst = 0; inst < settings[conf].algoInstances.size(); inst++) {
				if(settings[conf].algoInstances[inst].device == &dev) return 1 + conf;
			}
		}
		return 0;
	}

	asizei GetDeviceIndex(asizei setting, asizei instance) const { return settings[setting].algoInstances[instance].device->linearIndex; }


protected:
	AbstractCLAlgoImplementation(const char *name, const char *version) : AbstractAlgoImplementation(name, version) { }

	/*! Statically mangle the parameter set passed and pull out the values you understand. It's just as simple.
    Just parse it. Additional code around this will ensure the settings are unique, as long as the result is the same by operator==. */
	virtual void Parse(Options &opt, const std::vector<Settings::ImplParam> &params) = 0;

	asizei SelectSettings(const OpenCL12Wrapper::Platform &plat, const OpenCL12Wrapper::Device &dev) {
		return ChooseSettings(plat, dev, [](const char*){}); // doing that in the func declaration is a bit ugly
	}

	//! Create a new set of device-specific resources according to the selected settings and store them in target.
	virtual void BuildDeviceResources(Resources &target, cl_platform_id plat, cl_device_id dev, const Options &opt) = 0;

	//! Allocates an object of the derived class so this can copy inside its configs.
	virtual AbstractCLAlgoImplementation<NUM_STEPS, Options, Resources>* NewDerived() const = 0;

	/*! The DispatchData buffer is a small blob of data available to all kernels implementing CL. Some things are related to hashing
	and some others to supporting computation. This data is not always useful to all kernels, but handy to have. */
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

	//! \todo Some kernels might use midstate or not. Qubit doesn't, but it really should as first luffa loop is guaranteed to be the same thing.
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
		Resources res;
		asizei step;
		auint nonceBase;
		OpenCL12Wrapper::Device *device; //!< this is a very convenient way to have way more modular operations instead of a monolithic GenResources.
		stratum::WorkUnit wu; // job id, nonce2 information
		explicit AlgoInstance() : step(0), device(nullptr) { }
	};

	/*! All devices with the same options execute different AlgoInstance of an AlgoGroup (of similarly set devices).
	Each device can go its own way, albeit they'll be more or less coherent. */
	struct AlgoGroup {
		Options options;
		std::vector<AlgoInstance> algoInstances;
		explicit AlgoGroup() { }
		AlgoGroup(const Options &opt) : options(opt) { }
	};
	std::vector<AlgoGroup> settings; //!< \todo Also contains state so this is quite a bad name.
};
