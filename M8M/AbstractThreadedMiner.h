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

	static void CheckResults(std::vector<auint> &shares, AbstractAlgoImplementation<MiningProcessorsProvider> &algo, const stratum::WorkUnit &wu, const AbstractWorkSource *owner, asizei setting, asizei slot) {
		std::vector<auint> candidates;
		candidates.reserve(shares.size());
		std::array<aubyte, 128> header;
		std::array<aubyte, 32> hash;
		for(asizei test = 0; test < shares.size(); test++) {
			memcpy_s(header.data(), sizeof(header), wu.header.data(), sizeof(wu.header[0]) * wu.header.size());
			const auint lenonce = HTOLE(shares[test]);
			memcpy_s(header.data() + 64 + 12, 128 - (64 + 12), &lenonce, sizeof(lenonce));
			algo.HashHeader(hash, header, setting, slot);
			const aulong diffone = 0xFFFF0000ull * owner->GetCoinDiffMul();
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
		HashStats(asizei size) : start(size), count(size) { }
	};

	//! Returns the number of wait events added to the list.
    static asizei ProcessAI(AlgoMangling &mangle, WUSpecState &processing, HashStats &hash, WaitList &allWait, MPSamples &sampling, AsyncOutput &output) {
		asizei waiting = 0;
		stratum::WorkUnit foundWU;
		std::vector<auint> foundNonces;
		std::vector<cl_event> algoWait;
        if(mangle.algo.CanTakeInput(mangle.setting, mangle.res)) {
			auint prev = processing.hashesSoFar;
            hash.start[mangle.setting][mangle.res] = HPCLK::now();
            hash.count[mangle.setting] = mangle.algo.BeginProcessing(mangle.setting, mangle.res, processing.wu, processing.hashesSoFar);
            processing.hashesSoFar += hash.count[mangle.setting];
			if(processing.hashesSoFar <= prev) throw std::exception("nonce overflow, need to roll a new WU");
			//! \todo Really implement this. It really happens!
		}
		else if(mangle.algo.ResultsAvailable(foundWU, foundNonces, mangle.setting, mangle.res)) {
			using namespace std::chrono;
			HPTP resTime(HPCLK::now());
			const microseconds took = duration_cast<microseconds>(resTime - hash.start[mangle.setting][mangle.res]);

			if(sampling.sinceEpoch == seconds(0)) sampling.sinceEpoch = duration_cast<seconds>(hash.start[mangle.setting][mangle.res].time_since_epoch());
			const HPTP sampleStartTime = HPTP(duration_cast<HPTP::duration>(sampling.sinceEpoch));
			const microseconds toffset = duration_cast<microseconds>(hash.start[mangle.setting][mangle.res] - sampleStartTime);
			auto clamp = [](aulong value) -> auint { return auint(value < auint(~0)? value : auint(~0)); };
			sampling.Push(mangle.algo.GetDeviceIndex(mangle.setting, mangle.res), clamp(toffset.count()), clamp(took.count()));

            if(foundNonces.size()) {
                Nonces result;
                result.scanPeriod = took;
				result.owner = processing.owner; //!< \todo Not strictcly true, two pools might have the same job ID... to be fixed by looking up previous WUs? Most likely I could just push the owner to BeginProcessing... maybe also a timestamp.
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
        for(asizei init = 0; init < instances.size(); init++) hashStats.start[init].resize(instances[init].second);
        asizei numTasks = 0;
		std::for_each(instances.cbegin(), instances.cend(), [&numTasks](std::pair<asizei, asizei> add) { numTasks += add.second; });
		WUSpecState processing; // initially this was a vector and allowed to iterate on multiple pools. This is no more considered useful.
		asizei sinceLastCheck = 0;
		bool checkNonces = true;
		MPSamples sampling;
        cout<<"Miner thread completed initialization."<<endl; cout.flush();
		try {
			while(instances.size()) {
				if(processing.owner == nullptr) {
					// In those cases, there's nothing to do and I go to sleep till next check. Originally I used condition variables here but there's really little point in just having some polling.
					bool nothing;
					{
						std::unique_lock<std::mutex> sync(input.beingUsed);
						if(input.terminate) return;
						nothing = input.work.owner == nullptr;
					}
					if(nothing) {
						UpdateProfilerData(sampling);
						sleepFunc(1000);
						continue;
					}
				}
				/* Either we already have something to do or there might be something new to do (which could eventually be sleeping).
                That's not really different: it's just a matter of taking the work-unit the main thread gives us. */
				{
					std::unique_lock<std::mutex> sync(input.beingUsed);
					if(input.terminate) return;
					checkNonces = input.checkNonces;
                    if(processing.owner != input.work.owner || processing.wu != input.work.wu) {
                        processing.owner = input.work.owner;
                        processing.wu = input.work.wu;
                        if(processing.owner == nullptr) continue;
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
				for(asizei setting = 0; setting < instances.size(); setting++) {
                    for(asizei res = 0; res < instances[setting].second; res++) {
                        AlgoMangling process(algo, setting, res, checkNonces);
                        waiting += ProcessAI(process, processing, hashStats, allWait, sampling, output);
					}
				}
				if(waiting == numTasks)
					clWaitForEvents(allWait.size(), allWait.data());
				waiting = 0;
				allWait.clear();
				UpdateProfilerData(sampling);
			}
		} catch(std::exception ohno) {
			//! \todo decide how to forward this info to the main thread.
			cout<<"Ouch!"<<ohno.what()<<endl; cout.flush();
		} catch(...) {
			//! \todo decide how to forward this info to the main thread.
			cout<<"Ouch!"<<endl; cout.flush();
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
