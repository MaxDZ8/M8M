/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "MinerInterface.h"
#include "AlgoFamily.h"
#include <thread>
#include <mutex>
#include "MiningProfiling.h"


/*! This class sets up the basic logic to build a proper mining thread.
Because many operations involving massively parallel computing (especially building kernels) might take a while (even minutes)
there must be a way to do that without blocking this thread, which could be required to (example) do keepalive signals. 
This class however does not really take part in the "dynamic" driving of the thread, but tries to estabilish a common interface for thread communication.
\note None of those calls are performance paths. All the performance issues are contained in the miner thread and even there the main benefit for using
templates is to keep full type information. */
template<typename MiningProcessorsProvider>
class AbstractMiner : public MinerInterface, public AbstractMiningProfiler {
protected:
	struct WUSpec { //!< Used to keep track of input received by Mangle() so outputs can be fed to the right pool. \sa Nonces
        const AbstractWorkSource *owner;
        stratum::WorkUnit wu;
		explicit WUSpec() : owner(nullptr) { }
	};    

    struct AsyncInput { //!< Data from this thread to async mining thread.
        std::mutex beingUsed;
        WUSpec work; //!< this is the work unit the mining thread is mangling, use owner = nullptr to put it to sleep.
        bool terminate; //!< causes the mining thread to complete execution as fast as possible but gracefully and with proper release.
		bool checkNonces; //!< if this is false, hashing the nonces to verify them is not required and they're assumed valid.
		std::unique_ptr< AbstractAlgoImplementation<MiningProcessorsProvider> > run; //!< produced by the main thread but taken away by the worker thread asap
		explicit AsyncInput() : terminate(false), checkNonces(true) { }
	};
    
    struct AsyncOutput { //!< Data from the async mining thread.
        std::mutex beingUsed;
        std::vector<Nonces> found;
        bool terminated; //!< Guaranteed to be set to true at thread exit... supposed it started in the first place, no deadlocks occur and thread is not terminated forcefully
		explicit AsyncOutput() : terminated(false) { }
	};

	typedef std::function<void(typename AsyncInput &input, typename AsyncOutput &output, typename MiningProcessorsProvider::ComputeNodes procs)> MiningThreadFunc;
	virtual MiningThreadFunc GetMiningThread() = 0;

	//! Derived classes must build a MPSample struct asynchronously and call this every time possible as only sync point.
	//! If enough time has passed, the samples will be moved to the main thread and the passed struct will be emptied.
	void UpdateProfilerData(MPSamples &sampling) {
		const bool profilerEnabled = true;
		if(profilerEnabled) {
			using namespace std::chrono;
			const seconds flushInterval = seconds(5);
			seconds now = duration_cast<seconds>(system_clock::now().time_since_epoch());
			if(sampling.sinceEpoch.count() < now.count() - flushInterval.count()) {
				this->Push(sampling);
				sampling.Clear();
			}
		}
	}

private:
	std::vector< std::unique_ptr< AlgoFamily<MiningProcessorsProvider> > > algoFamilies;
	MiningProcessorsProvider hwProcessors;
	std::unique_ptr<std::thread> dispatcher;
	AbstractAlgoImplementation<MiningProcessorsProvider>* GetMiningAlgoImp() const {
		if(currAlgo.length() == 0) return nullptr;
		const char *algo = currAlgo.c_str();
		auto fam = std::find_if(algoFamilies.cbegin(), algoFamilies.cend(), [algo](const std::unique_ptr< AlgoFamily<MiningProcessorsProvider> > &test) {
			return test->AreYou(algo);
		});
		for(asizei loop = 0; loop < fam->get()->implementations.size(); loop++) {
			AbstractAlgoImplementation<MiningProcessorsProvider> &imp(*fam->get()->implementations[loop]);
			if(imp.AreYou(currImpl.c_str())) return &imp;
		}
		return nullptr;
	}

protected:
	std::string currAlgo, currImpl;
    AsyncInput toMiner;
	AsyncOutput fromMiner;    

public:
	AbstractMiner(std::vector< std::unique_ptr< AlgoFamily<MiningProcessorsProvider> > > &algos) : algoFamilies(std::move(algos)) { }
	~AbstractMiner() { // force clear in the correct order.
		if(dispatcher) {
			bool waitForQuit = true;
			{
				std::unique_lock<std::mutex> sync(toMiner.beingUsed);
				toMiner.terminate = true;
			}
			{
				time_t startChecking = time(NULL);
				while(startChecking + 10 < time(NULL)) {
					std::unique_lock<std::mutex> sync(fromMiner.beingUsed);
					if(fromMiner.terminated) break;
				}
				if(startChecking + 10 < time(NULL)) waitForQuit = false; // dead, not supposed to happen
				// ^ this is also very unlikely. If this happens, then the locks are probably inconsistently hold anyway and we never get here.
			}
			if(waitForQuit) dispatcher->join();
		}
		for(asizei loop = 0; loop < algoFamilies.size(); loop++) algoFamilies[loop]->Clear(hwProcessors);
		algoFamilies.clear();
	}

    std::string GetAlgos() const {
        std::string ret;
		for(auto el = algoFamilies.cbegin(); el != algoFamilies.cend(); ++el) {
            if(ret.length()) ret += ", ";
            ret += el->get()->GetName();
		}
        return ret;
	}
    std::string GetImplementations(const char *algo) const {
		auto match = std::find_if(algoFamilies.cbegin(), algoFamilies.cend(), [algo](const std::unique_ptr< AlgoFamily<MiningProcessorsProvider> > &test) {
			return test->AreYou(algo);
		});
		if(match == algoFamilies.cend()) return std::string();
        std::string ret;
		for(auto el = match->get()->implementations.cbegin(); el != match->get()->implementations.cend(); ++el) {
            if(ret.length()) ret += ", ";
            ret += (*el)->GetName();
		}
        return ret;
	}
	bool SetCurrentAlgo(const char *algo, const char *implementation) {
		if(currAlgo.length() || currImpl.length()) throw std::string("Miner already set up to run \"") + currAlgo + '.' + currImpl + ", algorithm switching not yet supported.";
		auto fam = std::find_if(algoFamilies.cbegin(), algoFamilies.cend(), [algo](const std::unique_ptr< AlgoFamily<MiningProcessorsProvider> > &test) {
			return test->AreYou(algo);
		});
		if(fam == algoFamilies.cend()) return false;
		auto imp = std::find_if(fam->get()->implementations.begin(), fam->get()->implementations.end(), [implementation](const AbstractAlgoImplementation<MiningProcessorsProvider> *test) {
			return test->AreYou(implementation);
		});
		if(imp == fam->get()->implementations.cend()) return false;
		currAlgo = algo;
		currImpl = implementation;
		return true;
	}
	const char* GetMiningAlgo() const {
		// Slightly more complicated because this way I can guarantee strings are persistent!
		if(currAlgo.length() == 0) return false;
		const char *algo = currAlgo.c_str();
		auto fam = std::find_if(algoFamilies.cbegin(), algoFamilies.cend(), [algo](const std::unique_ptr< AlgoFamily<MiningProcessorsProvider> > &test) {
			return test->AreYou(algo);
		});
		if(fam == algoFamilies.cend()) return nullptr; // likely because not yet set.
		return fam->get()->name;
	}
	bool GetMiningAlgoImpInfo(std::string &name, std::string &version) const {
		const AbstractAlgoImplementation<MiningProcessorsProvider> *imp = GetMiningAlgoImp();
		if(!imp) return false;
		name = imp->GetName();
		version = imp->GetVersion();
		return true;
	}

	void AddSettings(const std::vector<Settings::ImplParam> &params) {
		AbstractAlgoImplementation<MiningProcessorsProvider> *imp = GetMiningAlgoImp();
		// if(imp) { // given proper usage, this will always happen. If someone does not use it correctly, let him crash so he knows
		imp->AddSettings(params);
	}

	void Start() { 
		if(dispatcher) throw std::exception("This implementation does not allow mining to be restarted.");
		AbstractAlgoImplementation<MiningProcessorsProvider> *imp = GetMiningAlgoImp();
		imp->SelectSettings(hwProcessors.platforms);
		imp->MakeResourcelessCopy(toMiner.run); // note thread is guaranteed to be not running yet
		MiningThreadFunc worker(GetMiningThread());
		dispatcher.reset(new std::thread(worker, std::ref(toMiner), std::ref(fromMiner), GetProcessersProvider().platforms));
	}

	void Mangle(const AbstractWorkSource &owner, const stratum::WorkUnit &wu) {
        std::unique_lock<std::mutex> lock(toMiner.beingUsed);
		toMiner.work.owner = &owner;
		toMiner.work.wu = wu;
	}
		
	const AbstractWorkSource* GetCurrentPool() {
        std::unique_lock<std::mutex> lock(toMiner.beingUsed);
		return toMiner.work.owner;
	}

	bool SharesFound(std::vector<Nonces> &results) {
		std::unique_lock<std::mutex> lock(fromMiner.beingUsed);
		if(fromMiner.found.size() == 0) return false;
		results.reserve(results.size() + fromMiner.found.size());
		for(asizei loop = 0; loop < fromMiner.found.size(); loop++) results.push_back(fromMiner.found[loop]);
		fromMiner.found.clear();
		return true;
	}
	MiningProcessorsProvider& GetProcessersProvider() { return hwProcessors; }

	void CheckNonces(bool check) {
        std::unique_lock<std::mutex> lock(toMiner.beingUsed);
		toMiner.checkNonces = check;
	}
	bool GetDeviceConfig(asizei &config, asizei device) const {
		asizei linear = 0;
		const MiningProcessorsProvider::Device *devptr = nullptr;
		for(asizei plat = 0; plat < hwProcessors.platforms.size(); plat++) {
			if(device < hwProcessors.platforms[plat].devices.size()) {
				devptr = &hwProcessors.platforms[plat].devices[device];

			}
			device -= hwProcessors.platforms[plat].devices.size();
		}
		if(!devptr) return false;
		const AbstractAlgoImplementation<MiningProcessorsProvider> *imp = GetMiningAlgoImp();
		if(!imp) return false; // impossible
		config = imp->GetDeviceUsedConfig(*devptr);
		return true;
	}
	std::vector<std::string> GetBadConfigReasons(asizei devIndex) const {
		AbstractAlgoImplementation<MiningProcessorsProvider> *imp = GetMiningAlgoImp();
		std::vector<std::string> dummy;
		if(!imp) {
			dummy.push_back("No algorithm implementation to mine");
			return dummy;
		}
		MiningProcessorsProvider::Device *device = hwProcessors.GetDeviceLinear(devIndex);
		if(!device) {
			dummy.push_back("Device [" + std::to_string(devIndex) + "] does not exist");
			return dummy;
		}
		MiningProcessorsProvider::Platform *plat = hwProcessors.GetPlatform(*device);
		if(!plat) {
			dummy.push_back("Device [" + std::to_string(devIndex) + "] not found in any platform (impossible)");
			return dummy;
		}
		return imp->GetBadConfigReasons(*plat, *device);
	}
	const AlgoImplementationInterface* GetAI(const char *family, const char *impl) const {
		for(asizei loop = 0; loop < algoFamilies.size(); loop++) {
			if(algoFamilies[loop]->AreYou(family)) {
				for(asizei inner = 0; inner < algoFamilies[loop]->implementations.size(); inner++) {
					if(algoFamilies[loop]->implementations[inner]->AreYou(impl)) {
						return algoFamilies[loop]->implementations[inner];
					}
				}
			}
		}
		return nullptr;
	}
};
