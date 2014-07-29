/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractMiner.h"
#include <set>
#include <chrono>

template<typename MiningProcessorsProvider>
class AbstractThreadedMiner : public AbstractMiner<MiningProcessorsProvider> {

	struct DelayRequest {
		std::chrono::time_point<std::chrono::high_resolution_clock> when;
		auint howManyMS;
		void Clear() { when = std::chrono::time_point<std::chrono::high_resolution_clock>();    howManyMS = 0; }
		void Requested(auint ms) { when = std::chrono::high_resolution_clock::now(), howManyMS = ms; }
		auint Remaining() const {
			auto now = std::chrono::high_resolution_clock::now();
			auto elapsed = now - when;
			if(elapsed.count() >= howManyMS * .001) return 0;
			return auint((howManyMS * .001 - elapsed.count()) * 1000);
		}
		DelayRequest() { Clear(); }
	};

	struct WUSpecState : public WUSpec {
		auint hashesSoFar;
		WUSpecState() : hashesSoFar(0) { }
	};

	static void CheckResults(std::vector<auint> &shares, const WUSpec &algo) {
		std::vector<auint> candidates;
		candidates.reserve(shares.size());
		std::array<aubyte, 128> header;
		std::array<aubyte, 32> hash;
		for(asizei test = 0; test < shares.size(); test++) {
			memcpy_s(header.data(), sizeof(header), algo.wu.header.data(), sizeof(algo.wu.header[0]) * algo.wu.header.size());
			const auint lenonce = HTOLE(shares[test]);
			memcpy_s(header.data() + 64 + 12, 128 - (64 + 12), &lenonce, sizeof(lenonce));
			algo.params.imp->HashHeader(hash, header, algo.params.internalIndex);
			const aulong diffone = 0xFFFF0000ull * algo.owner->GetCoinDiffMul();
			aulong adjusted;
			memcpy_s(&adjusted, sizeof(adjusted), hash.data() + 24, sizeof(adjusted));
			if(LETOH(adjusted) <= diffone) candidates.push_back(lenonce); // could still be stale
		}
		shares = std::move(candidates);
	}	
    //std::function<void(AsyncInput &input, AsyncOutput &output, MiningProcessorsProvider &resourceGenerator)> GetMiningThread() const;

public:
	AbstractThreadedMiner(std::vector< std::unique_ptr< AlgoFamily<MiningProcessorsProvider> > > &algos, const std::function<void(auint sleepms)> sleepFunc)
		: AbstractMiner(algos) {
		MiningThreadFunc asyncronousMining = [sleepFunc](AsyncInput &input, AsyncOutput &output, MiningProcessorsProvider &resourceGenerator) {
			ScopedFuncCall imdone([&output]() { 
				std::unique_lock<std::mutex> sync(output.beingUsed);
				output.terminated = true;
			});
			cout<<"Miner thread starting."<<endl; cout.flush();
			std::vector<WUSpecState> processing;
			asizei sinceLastCheck = 0;
			bool checkNonces = true;
			try {
				while(true) {
					if(processing.empty()) {
						// In those cases, there's nothing to do and I go to sleep till next check. Originally I used condition variables here but there's really little point in just having some polling.
						bool nothing = true;
						{
							std::unique_lock<std::mutex> sync(input.beingUsed);
							if(input.terminate) return;
							nothing = input.work.empty();
						}
						if(nothing) {
							sleepFunc(1000);
							continue;
						}
					}
					// either we already have something to do or there might be something new to do.
					{
						std::unique_lock<std::mutex> sync(input.beingUsed);
						if(input.terminate) return;
						checkNonces = input.checkNonces;
						while(input.work.size()) {
							const WUSpec &el(input.work.front());
							const std::vector<WUSpecState>::iterator prev(std::find_if(processing.begin(), processing.end(), [&el](const WUSpecState &test) { return test.owner == el.owner; }));
							if(prev != processing.end()) {
								if(prev->params != el.params) {
									if(el.params.imp == nullptr) std::exception("Source being unregistered, not currently implemented.");
									throw std::exception("Source requires algorithm change, not currently implemented.");
								}
								prev->wu = el.wu;
								prev->hashesSoFar = 0;
							}
							else { // new source
								WUSpecState newWork;
								newWork.owner = el.owner;
								newWork.wu = el.wu;
								newWork.params = el.params;
								processing.push_back(newWork);
							}
							input.work.pop_front();
						}
					}
					// From now on, I don't care about the input anymore as everything is in my private list.
					// As a first thing, initialize algorithms not ready to go. For the time being, this will halt mining.
					for(asizei loop = 0; loop < processing.size(); loop++) {
						if(processing[loop].params.imp->Ready(processing[loop].params.internalIndex) == false) {
							processing[loop].params.imp->Prepare(processing[loop].params.internalIndex);
						}
					}
					/*Now the real work and we're done. This is still not so easy as each algorithm:
					1) might be temporarily disabled (ready to go, but no-op work unit)
					2) might be waiting for a delay to pass before going on for the results
					3) might be able to accept new inputs (because of internal pipelining?)
					4) could be able to output resulting shares.
					I give each algorithm a single chance to perform an action. Other actions will be delayed
					till next pass. */
					std::vector<cl_event> allWait;
					asizei waiting = 0;
					for(asizei loop = 0; loop < processing.size(); loop++) {
						WUSpecState &el(processing[loop]);
						if(el.wu.genTime == 0 || el.wu.ntime == 0) {
							// no-op workload, this is considered a delay pass until this algo gets something to do.
							// It will likely take a while but 1 second is likely enough.
							sleepFunc(1000);
							continue;
						}
						std::vector<auint> foundNonces;
						stratum::WorkUnit foundWU;
						std::vector<cl_event> algoWait;
						if(el.params.imp->CanTakeInput(el.params.internalIndex)) {
							auint prev = el.hashesSoFar;
							el.hashesSoFar += el.params.imp->BeginProcessing(el.params.internalIndex, el.wu, el.hashesSoFar);
							if(el.hashesSoFar <= prev) throw std::exception("nonce overflow, need to roll a new WU");
						}
						else if(el.params.imp->ResultsAvailable(foundWU, foundNonces, el.params.internalIndex) && el.wu.job == foundWU.job) {
							// if job id is not matched, those results are stale for sure.
							if(checkNonces) CheckResults(foundNonces, el);
							if(foundNonces.size()) {
								std::unique_lock<std::mutex> sync(output.beingUsed);
								Nonces add;
								add.owner = el.owner;
								add.job = foundWU.job;
								add.nonce2 = foundWU.nonce2;
								add.nonces = std::move(foundNonces);
								output.found.push_back(add);
							}
						}
						else if(el.params.imp->GetWaitEvents(algoWait, el.params.internalIndex)) {
							waiting++;
							for(auto el = algoWait.begin(); el != algoWait.end(); ++el) allWait.push_back(*el);
						}
						else el.params.imp->Dispatch();
					}
					if(waiting == processing.size())
						clWaitForEvents(allWait.size(), allWait.data());
					waiting = 0;
					allWait.clear();
				}
			} catch(...) {
				//! \todo decide how to forward this info to the main thread.
			}
			cout<<"Miner thread exiting!"<<endl; cout.flush();
		};
		dispatcher.reset(new std::thread(asyncronousMining, std::ref(toMiner), std::ref(fromMiner), std::ref(GetProcessersProvider())));
		sleepFunc(1); // give the other thread a chance to go!
	}
};
