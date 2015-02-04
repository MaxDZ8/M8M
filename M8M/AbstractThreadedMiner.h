/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractMiner.h"

template<typename MiningProcessorsProvider>
class AbstractThreadedMiner : public AbstractMiner<MiningProcessorsProvider> {
	struct DelayRequest {
		HPTP when;
		auint howManyMS;
		void Clear() { when = HPTP();    howManyMS = 0; }
		void Requested(auint ms) { when = HRCLK::now(), howManyMS = ms; }
		auint Remaining() const {
			auto now = HRCLK::now();
			auto elapsed = now - when;
			if(elapsed.count() >= howManyMS * .001) return 0;
			return auint((howManyMS * .001 - elapsed.count()) * 1000);
		}
		DelayRequest() { Clear(); }
	};

	struct WUSpecState : public WUSpec {
		auint hashesSoFar;
		const HPTP creationTime;
		asizei iterations;
		explicit WUSpecState() : hashesSoFar(0), creationTime(HPCLK::now()), iterations(0) { }
	};

	static void CheckResults(std::vector<auint> &shares, AbstractAlgoImplementation<MiningProcessorsProvider> &algo, const typename AbstractAlgoImplementation<MiningProcessorsProvider>::IterationStartInfo &wu, const AbstractWorkSource &owner, asizei setting, asizei slot) {
		std::vector<auint> candidates;
		candidates.reserve(shares.size());
		std::array<aubyte, 128> header;
		std::array<aubyte, 32> hash;
		for(asizei test = 0; test < shares.size(); test++) {
			memcpy_s(header.data(), sizeof(header), wu.header.data(), sizeof(wu.header[0]) * wu.header.size());
			const auint lenonce = HTOLE(shares[test]);
			memcpy_s(header.data() + 64 + 12, 128 - (64 + 12), &lenonce, sizeof(lenonce));
			algo.HashHeader(hash, header, setting, slot);
			const aulong diffone = 0xFFFF0000ull * owner.GetDiffMultipliers().share;
			aulong adjusted;
			memcpy_s(&adjusted, sizeof(adjusted), hash.data() + 24, sizeof(adjusted));
			if(LETOH(adjusted) <= diffone) candidates.push_back(lenonce); // could still be stale
		}
		shares = std::move(candidates);
	}
    
    struct AlgoMangling {
        AbstractAlgoImplementation<MiningProcessorsProvider> &algo;
        asizei setting, res;
		const bool checkNonces;
        AlgoMangling(AbstractAlgoImplementation<MiningProcessorsProvider> &what, asizei optIndex, asizei instIndex, bool nonceCheck)
			: algo(what), setting(optIndex), res(instIndex), checkNonces(nonceCheck) { }
	};

	typedef std::vector<typename MiningProcessorsProvider::WaitEvent> WaitList;

	struct HashStats {
		std::vector< std::vector<HPTP> > start;
		std::vector<asizei> count;
		std::vector< std::vector<HPTP> > lastNonceFound;
		std::vector< std::vector<asizei> > numIterations;
		HashStats(asizei size) : start(size), count(size), lastNonceFound(size), numIterations(size) { }
	};

	struct OwnedWU {
		const AbstractWorkSource &owner;
		stratum::AbstractWorkUnit &wu;
		auint &hashesSoFar;
		OwnedWU(const AbstractWorkSource &aws, stratum::AbstractWorkUnit &swu, auint &nonce) : owner(aws), wu(swu), hashesSoFar(nonce) { }
	};

	//! Returns the number of wait events added to the list.
    static asizei ProcessAI(AlgoMangling &mangle, OwnedWU &processing, HashStats &hash, WaitList &allWait, AsyncOutput &output) {
		asizei waiting = 0;
		AbstractAlgoImplementation<MiningProcessorsProvider>::IterationStartInfo foundWU;
		std::vector<auint> foundNonces;
		std::vector<cl_event> algoWait;
        if(mangle.algo.CanTakeInput(mangle.setting, mangle.res)) {
			auint prev = processing.hashesSoFar;
            hash.start[mangle.setting][mangle.res] = HPCLK::now();
            hash.count[mangle.setting] = mangle.algo.BeginProcessing(mangle.setting, mangle.res, processing.wu, processing.hashesSoFar);
			hash.numIterations[mangle.setting][mangle.res]++;
            processing.hashesSoFar += hash.count[mangle.setting];
			if(processing.hashesSoFar <= prev) {
				std::cout<<"**** Rolling nonce2: "<<processing.wu.nonce2<<" -> "<<processing.wu.nonce2 + 1<<" ****"<<std::endl;
				asizei prev = processing.wu.nonce2;
				processing.wu.nonce2++;
				if(processing.wu.nonce2 <= prev) throw std::exception("nonce2 overflow... wait WHAT???");
				processing.wu.MakeNoncedHeader(mangle.algo.littleEndianAlgo);
				processing.hashesSoFar = 0;
			}
		}
		else if(mangle.algo.ResultsAvailable(foundWU, foundNonces, mangle.setting, mangle.res)) {
			using namespace std::chrono;
			HPTP resTime(HPCLK::now());
			const microseconds took = duration_cast<microseconds>(resTime - hash.start[mangle.setting][mangle.res]);
            if(foundNonces.size()) {
                Nonces result;
                result.scanPeriod = took;
				result.owner = &processing.owner; //!< \todo Not strictcly true, two pools might have the same job ID... to be fixed by looking up previous WUs? Most likely I could just push the owner to BeginProcessing... maybe also a timestamp.
				result.job = foundWU.job;
				result.nonce2 = foundWU.nonce2;
                result.deviceIndex = mangle.algo.GetDeviceIndex(mangle.setting, mangle.res);
				result.lastNonceScanAmount = hash.count[mangle.setting];
                if(processing.wu.job != foundWU.job) result.stale = foundNonces.size();
				else {
                    const asizei found = foundNonces.size();
                    if(mangle.checkNonces) CheckResults(foundNonces, mangle.algo, foundWU, processing.owner, mangle.setting, mangle.res);
                    result.bad = found - foundNonces.size();
				}
				asizei &numIterations(hash.numIterations[mangle.setting][mangle.res]);
				HPTP &lastNonceFound(hash.lastNonceFound[mangle.setting][mangle.res]);
				microseconds elapsed = duration_cast<microseconds>(hash.start[mangle.setting][mangle.res] - lastNonceFound);
				result.avgSLR = microseconds(elapsed.count() / numIterations);
				numIterations = 0;
				lastNonceFound = resTime;
			    std::unique_lock<std::mutex> sync(output.beingUsed);
				result.nonces = std::move(foundNonces);
				output.found.push_back(result);
			}
		}
		else if(mangle.algo.GetWaitEvents(algoWait, mangle.setting, mangle.res)) {
            const asizei prev = allWait.size();
			waiting++;
            allWait.resize(prev + algoWait.size());
			for(asizei cp = 0; cp < algoWait.size(); cp++) allWait[prev + cp] = algoWait[cp];
		}
		else mangle.algo.Dispatch(mangle.setting, mangle.res);
		return waiting;
	}

	void ThreadedMinerMain(AsyncInput &input, AsyncOutput &output, typename MiningProcessorsProvider::ComputeNodes processors) {
        //! \todo no way! Resource generation is not supposed to work this way, it is not thread protected! All I need is a list of platforms and devices to build resources.
		ScopedFuncCall imdone([&output]() { 
			std::unique_lock<std::mutex> sync(output.beingUsed);
			output.terminated = true;
		});
		cout<<"Miner thread starting."<<endl; cout.flush();
        std::unique_ptr< AbstractAlgoImplementation<MiningProcessorsProvider> > run;
        {
			std::unique_lock<std::mutex> sync(input.beingUsed);
			run = std::move(input.run);
		}
        AbstractAlgoImplementation<MiningProcessorsProvider> &algo(*run);
        std::vector< std::pair<asizei, asizei> > instances(algo.GenResources(processors));
		HashStats hashStats(instances.size());
        {
			HPTP now(HPCLK::now());
			for(asizei init = 0; init < instances.size(); init++) {
				hashStats.start[init].resize(instances[init].second);
				hashStats.lastNonceFound[init].resize(instances[init].second);
				hashStats.numIterations[init].resize(instances[init].second);
				for(asizei cp = 0; cp < instances[init].second; cp++) {
					hashStats.numIterations[init][cp] = 0;
					hashStats.lastNonceFound[init][cp] = now;
				}
			}
		}
        asizei numTasks = 0;
		std::for_each(instances.cbegin(), instances.cend(), [&numTasks](std::pair<asizei, asizei> add) { numTasks += add.second; });
		const AbstractWorkSource *owner = nullptr;
		std::unique_ptr<stratum::AbstractWorkUnit> mangling;
		asizei sinceLastCheck = 0;
		bool checkNonces = true;
		{
			std::unique_lock<std::mutex> sync(output.beingUsed);
			output.initialized = true;
		}
		auint testedNonces = 0;
		std::string exceptionDesc;
		try {
			while(instances.size()) {
				if(true) { // maybe only lock every N iterations? Every 1 second?
					std::unique_lock<std::mutex> sync(input.beingUsed);
					if(input.terminate) return;
					checkNonces = input.checkNonces;
					owner = input.owner;
					if(input.wu) {
						auint prevN2 = mangling? mangling->nonce2 : 0;
						mangling = std::move(input.wu);
						if(mangling->restart == false) mangling->nonce2 = prevN2;
						mangling->MakeNoncedHeader(algo.littleEndianAlgo);
					}
					if(owner == nullptr || mangling == nullptr) {
						sleepFunc(1000);
						continue;
					}
				}
                /* Now I don't have to care about sync-ing anymore as as everything is in my private stack/pointers.
				Initially, algorithms had to be initialized here as they used to be registered and de-registered dynamically but
                this didn't make much sense as estimating hashrate would have been nonsensical.
                So what I really need to do at this point is to iterate on the various algorithm configs and for each available
                set of resource (which is a concurrent algorithm instance) evolve it a bit. Each of those entities might be: 
				1) waiting for a delay to pass before going on for the results
				2) able to accept new inputs (could be pipelined)
				3) able to output resulting shares
                4) just willing to do some work!
				I give each algorithm a single chance to perform an action. Other actions will be delayed till next pass. */
				std::vector<MiningProcessorsProvider::WaitEvent> allWait;
				asizei waiting = 0;
				OwnedWU processing(*owner, *mangling, testedNonces);
				for(asizei setting = 0; setting < instances.size(); setting++) {
                    for(asizei res = 0; res < instances[setting].second; res++) {
                        AlgoMangling process(algo, setting, res, checkNonces);
                        if(ProcessAI(process, processing, hashStats, allWait, output)) waiting++;
					}
				}
				if(waiting == numTasks) {
					clWaitForEvents(cl_uint(allWait.size()), allWait.data());
				}
			}
		} catch(std::exception ohno) {
			cout<<"Ouch!"<<ohno.what()<<endl; cout.flush();
			exceptionDesc = ohno.what();
		} catch(std::string ohno) {
			cout<<"Ouch!"<<ohno<<endl; cout.flush();
			exceptionDesc = std::move(ohno);
		} catch(...) {
			cout<<"Ouch fatal error in miner thread processing, and I don't even know what it is!"<<endl; cout.flush();
		}
		if(exceptionDesc.length()) {
			std::unique_lock<std::mutex> sync(input.beingUsed);
			output.error.second = std::move(exceptionDesc);
			output.error.first = true;
		}
		cout<<"Miner thread exiting!"<<endl; cout.flush();
	}


    MiningThreadFunc GetMiningThread() {
		return [this](AsyncInput &input, AsyncOutput &output, typename MiningProcessorsProvider::ComputeNodes processors) {
			ThreadedMinerMain(input, output, processors);
		};
	}

    std::function<void(auint sleepms)> sleepFunc;

public:
	AbstractThreadedMiner(std::vector< std::unique_ptr< AlgoFamily<MiningProcessorsProvider> > > &algos, const std::function<void(auint sleepms)> msSleepFunc)
		: AbstractMiner(algos), sleepFunc(msSleepFunc) { }
};
