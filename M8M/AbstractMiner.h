/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "MinerInterface.h"
#include "AlgoFamily.h"
#include <mutex>
#include <deque>


/*! This class sets up the basic logic to build a proper Miner thread.
It all revolves around driving a second thread which must be managed with care.
Because many operations involving massively parallel computing (especially building kernels) might take a while (even minutes)
there must be a way to do that without blocking this thread, which could be required to (example) do keepalive signals. 
This class however does not really take part in the "dynamic" driving of the thread, but tries to estabilish a common interface for thread communication.
\note None of those calls are performance paths. All the performance issues are contained in the miner thread and even there the main benefit for using
templates is to keep full type information. */
template<typename MiningProcessorsProvider>
class AbstractMiner : public MinerInterface {
protected:
    struct AlgoSetting {
        AbstractAlgoImplementation<MiningProcessorsProvider> *imp;
        asizei internalIndex;
		explicit AlgoSetting() : imp(nullptr) { }
		bool operator!=(const AlgoSetting &other) const { return imp != other.imp || internalIndex != other.internalIndex; }
	};
    struct WUSpec {
        const AbstractWorkSource *owner;
        stratum::WorkUnit wu;
        AlgoSetting params;
		explicit WUSpec() : owner(nullptr) { }
	};    

    struct AsyncInput { //!< Data from this thread to async mining thread.
        std::mutex beingUsed;
        std::deque<WUSpec> work; //!< this is really a consume queue, new WUSpecs are put here, the miner thread pulls them off so when this is empty, there's no NEW WORK to do.
        bool terminate; //!< causes the mining thread to complete execution as fast as possible but gracefully and with proper release.
		bool checkNonces; //!< if this is false, hashing the nonces to verify them is not required and they're assumed valid.
		explicit AsyncInput() : terminate(false) { }
	};
    
    struct AsyncOutput { //!< Data from the async mining thread.
        std::mutex beingUsed;
        std::vector<Nonces> found;
        bool terminated; //!< Guaranteed to be set to true at thread exit... supposed it started in the first place, no deadlocks occur and thread is not terminated forcefully
		explicit AsyncOutput() : terminated(false) { }
	};

	// Not used, see ctor
    //virtual std::function<void(AsyncInput &input, AsyncOutput &output, MiningProcessorsProvider &resourceGenerator)> GetMiningThread() const = 0;

	typedef std::function<void(AsyncInput &input, AsyncOutput &output, MiningProcessorsProvider &resourceGenerator)> MiningThreadFunc;

private:
    std::vector<AlgoSetting> settings;
	std::vector< std::unique_ptr< AlgoFamily<MiningProcessorsProvider> > > algoFamilies;
	MiningProcessorsProvider hwProcessors;

protected:
    AsyncInput toMiner;
	AsyncOutput fromMiner;    
	std::unique_ptr<std::thread> dispatcher;

public:
	AbstractMiner(std::vector< std::unique_ptr< AlgoFamily<MiningProcessorsProvider> > > &algos) : algoFamilies(std::move(algos)) {
		// In line of concept I would do something like that but
		// No derived class override in ctor
		// Don't want to have the func passed as parameter (terrible syntax)
		// Therefore, this is now derived class responsability.
		//
		//dispatcher.reset(new std::thread(GetMiningThread(), std::ref(toMiner), std::ref(fromMiner), std::ref(hwProcessors)));
	}
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
    asizei EnableAlgorithm(const char *algo, const char *implementation, const std::vector<Settings::ImplParam> &params) {
		auto fam = std::find_if(algoFamilies.cbegin(), algoFamilies.cend(), [algo](const std::unique_ptr< AlgoFamily<MiningProcessorsProvider> > &test) {
			return test->AreYou(algo);
		});
		if(fam == algoFamilies.cend()) return asizei(-1);
		auto imp = std::find_if(fam->get()->implementations.begin(), fam->get()->implementations.end(), [implementation](const AbstractAlgoImplementation<MiningProcessorsProvider> *test) {
			return test->AreYou(implementation);
		});
		if(imp == fam->get()->implementations.cend()) return asizei(-1);
		settings.reserve(settings.size() + 1);
		AlgoSetting add;
		add.imp = *imp;
		add.internalIndex = (*imp)->ValidateSettings(hwProcessors, params);
        settings.push_back(add);
        return settings.size() - 1;
	}

	void Mangle(const AbstractWorkSource &owner, const stratum::WorkUnit &wu, const auint algoSettings) {
		if(algoSettings >= settings.size()) throw std::exception("Invalid algorithm settings requested.");
        std::unique_lock<std::mutex> lock(toMiner.beingUsed);
        for(asizei loop = 0; loop < toMiner.work.size(); loop++) { // just in case the miner didn't had the chance to mangle them yet (very unlikely)
            if(toMiner.work[loop].owner == &owner) {
                toMiner.work[loop].wu = wu;
                toMiner.work[loop].params = settings[algoSettings];
                return;                
			}
		}
        WUSpec add;
        add.owner = &owner;
        add.wu = wu;
        add.params = settings[algoSettings];
        toMiner.work.push_back(add);
	}
	bool SharesFound(std::vector<Nonces> &results) {
        std::unique_lock<std::mutex> lock(fromMiner.beingUsed);
        if(fromMiner.found.size() == 0) return false;
        for(asizei loop = 0; loop < fromMiner.found.size(); loop++) results.push_back(fromMiner.found[loop]);
        fromMiner.found.clear();
        return true;
	}
	MiningProcessorsProvider& GetProcessersProvider() { return hwProcessors; }

	void CheckNonces(bool check) {
        std::unique_lock<std::mutex> lock(toMiner.beingUsed);
		toMiner.checkNonces = check;
	}
};
