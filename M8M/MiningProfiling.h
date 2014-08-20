/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/AREN/ArenDataTypes.h"
#include <vector>
#include <queue>
#include <chrono>
#include <thread>
#include <mutex>

/*! Objects implementing this interface allow to collect fine-grained statistics about mining devices performance.
This data is already made available every time a share is produced but in some cases it might not be enough.
VARDIFF pools usually adjust the difficulty so ~1 share is produced each minute. This is insufficient if we want to investiate
hardware performance or if we just want to make a silly graph. :P */
class MiningProfilerInterface {
public:
	virtual ~MiningProfilerInterface() { }

	/*! It works this way: the miner thread collects its data continuously without locking. Every once in a while, when it got enough
	data, it locks once to its internal structures implementing MiningProfilerInterface and uploads one of those structs.
	\note Those structures are currently copied. They are really meant to be as I want them to be lightweight on mining thread client which
	will get a spike of activity when copying... it's not expected to have much data anyway! */
	struct MPSamples {
		std::chrono::seconds sinceEpoch;

		/*! I put everything here to save on the number of realloc.
		- [3n+0] is the device linear index reporting hashrate. Who produced this value.
		- [3n+1] is value timing WRT initial displacement. When the value was produced, microseconds.
		- [3n+2] is the scan hash time, in microseconds. */
		std::vector<auint> interleaved;

		void Push(auint producerID, auint tdisp, auint scanHashT) {
			interleaved.reserve(interleaved.size() + 3);
			interleaved.push_back(producerID);
			interleaved.push_back(tdisp);
			interleaved.push_back(scanHashT);
		}
		void Get(auint &linearIndex, auint &tdisp, auint &scanHashT, asizei slot) {
			linearIndex = interleaved[slot * 3 + 0];
			tdisp       = interleaved[slot * 3 + 1];
			scanHashT   = interleaved[slot * 3 + 2];
		}
		void Clear() { sinceEpoch = sinceEpoch.zero();    interleaved.clear(); }
	};

	/*! Sync point. Pull out a set of values, giving up ownership. Returns false if the internal queue was empty. */
	virtual bool Pop(MPSamples &oldest) = 0;

	/*! 0 = keep indefinetely, otherwise number of max seconds. Is it useful to have it changed on the fly? 
	This is meant to be tested against Samples::sinceEpoch so it's fairly gross grained but it shouldn't be much of a problem. */
	virtual void SetMaxAge(const std::chrono::seconds &secs) = 0;

	/*! Call this every once in a while to prune old data. */
	virtual void Tick() = 0;
};


// Multiple inheritance and non-interface shall be taken with some care but this isn't expected to be a problem ever.
class AbstractMiningProfiler : public MiningProfilerInterface {
	std::mutex sync;
	std::queue<MPSamples> data;
	std::chrono::seconds maxAge;

	void OldiesAway() {
		using namespace std::chrono;
		seconds now(duration_cast<seconds>(system_clock::now().time_since_epoch()));
		while(data.empty() == false && maxAge.count()) {
			if(data.front().sinceEpoch > now - maxAge) break;
			data.pop();
		}
	}

public:
	AbstractMiningProfiler() : maxAge(60) { }
	bool Pop(MPSamples &oldest) {
		std::unique_lock<std::mutex> lock(sync);
		if(data.empty()) return false;
		MPSamples &front(data.front());
		if(front.sinceEpoch == std::chrono::seconds(0)) { // spurious, it happens sometimes
			data.pop();
			return false;
		}
		oldest.sinceEpoch = front.sinceEpoch; // those are typicall ulongs anyway
		oldest.interleaved = std::move(front.interleaved);
		data.pop();
		return true;
	}
	void SetMaxAge(const std::chrono::seconds &secs) {
		std::unique_lock<std::mutex> lock(sync); // because we might have a Push going on, maybe cleaning
		bool shorter = secs < maxAge;
		maxAge = secs;
		if(shorter) OldiesAway();
	}
	void Push(MPSamples fresh) {
		std::unique_lock<std::mutex> lock(sync);
		OldiesAway();
		data.push(fresh);
	}
	void Tick() { std::unique_lock<std::mutex> lock(sync);    OldiesAway(); }
};
