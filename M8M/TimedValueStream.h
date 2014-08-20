/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <queue>
#include <chrono>
#include <vector>
#include "../Common/AREN/ArenDataTypes.h"

/*! This object keeps track of iteration time from a device.
Every time a device produces a share, the main thread mangles them and tells this object how many seconds it took to test the hashes.
It keeps track of last time and average across twindow and twindow * longTerm minutes since last sample was taken.
Average is not really interesting: it tends to stabilize after a few minutes anyway.
Also keep min and max time. 
As this is not really device-specific, there's no device in it... it stores a flexible FIFO of values where old values get out
when they are too old, otherwise it keeps growing. */
class TimedValueStream {
public:
	typedef std::chrono::system_clock::time_point TimePoint; //!< used to mark when I got a new stat
	
	TimedValueStream(auint windowDuration = 5, aushort longTerm = 6)
		: tshort(windowDuration), tlong(windowDuration * longTerm), longest(0) {
		TimePoint epoch(std::chrono::system_clock::from_time_t(time_t(0)));
		auto longTime = std::chrono::system_clock::now() - epoch;
		shortest = std::chrono::duration_cast<std::chrono::microseconds>(longTime);
	}

	void Took(const std::chrono::microseconds &t) {
		Sample add;
		add.received = std::chrono::system_clock::now();
		add.took = t;
		Sample old;
		if(Push(old, shortw, add, tshort)) Push(old, longw, old, tlong);
		if(t > longest) longest = t;
		if(t < shortest) shortest = t;

	}
	std::chrono::microseconds GetLast() const { 
		if(shortw.sample.size() == 0) return std::chrono::microseconds(0);
		asizei last = shortw.old - 1;
		last %= shortw.sample.size();
		return std::chrono::microseconds(shortw.sample[last].took); 
	}
	std::chrono::microseconds GetShortAverage() const {
		if(!shortw.len) std::chrono::microseconds(0);
		return std::chrono::microseconds(aulong(Average(shortw.sample)));
	}
	std::chrono::microseconds GetLongAverage() const {
		if(!longw.len) return GetShortAverage();
		const double early = Average(shortw.sample);
		const double late = Average(longw.sample);
		const asizei both = shortw.sample.size() + longw.sample.size();
		double wearly = double(shortw.sample.size()) / double(both);
		double wlate = double(longw.sample.size()) / double(both);
		double avg = early * wearly + late * wlate;
		return std::chrono::microseconds(aulong(avg));
	}

	void ResetMinmax() {
		longest = std::chrono::microseconds();
		TimePoint epoch(std::chrono::system_clock::from_time_t(time_t(0)));
		auto longTime = std::chrono::system_clock::now() - epoch;
		shortest = std::chrono::duration_cast<std::chrono::microseconds>(longTime);
	}

	std::chrono::microseconds GetMin() const { return shortw.len? shortest : std::chrono::microseconds(0); }
	std::chrono::microseconds GetMax() const { return longest; }

	std::chrono::minutes GetShortWindow() const { return tshort; }
	std::chrono::minutes GetLongWindow() const { return tlong; }

private:
	std::chrono::minutes tshort, tlong; //!< initially const. Does it make sense to allow them to be changed dynamically?
	std::chrono::microseconds shortest, longest;

	struct Sample {
		TimePoint received;
		std::chrono::microseconds took;
	};
	/*! Samples get put there as soon as they are received. Old samples get pulled out if they exceed the 5-min time window. */
	struct SlidingWindow {
		std::vector<Sample> sample;
		asizei old, len;
		SlidingWindow() : old(0), len(0) { }
		Sample& Oldest() { return sample[old]; }
		void ResetOld(const Sample &val) { sample[old++] = val;    old %= sample.size(); }
	} shortw, longw;

	//! Does really work only for positive durations.
	static bool Push(Sample &out, SlidingWindow &sliding, const Sample &last, const std::chrono::seconds &maxAge) {
		if(!sliding.sample.size()) {
			sliding.sample.push_back(last);
			sliding.len++;
			return false;
		}
		using std::chrono::system_clock;
		out = sliding.Oldest();
		if(system_clock::now() - out.received > maxAge) {
			sliding.ResetOld(last);
			return true;
		}
		// This happens if we have a burst of samples before one sample expires.
		if(sliding.old + sliding.len == sliding.sample.size()) sliding.sample.push_back(last);
		else {
			sliding.sample.push_back(sliding.sample.back());
			for(asizei n = sliding.sample.size() - 2; n > sliding.old; n--) sliding.sample[n] = sliding.sample[n - 1];
			sliding.ResetOld(last);
		}
		sliding.len++;
		return false;
	}

	static double Average(const std::vector<Sample> &all) {
		if(all.size() == 0) return .0; // not sampled yet, makes more sense
		double sum = .0;
		double weight = 1.0 / double(all.size());
		for(asizei loop = 0; loop < all.size(); loop++) sum += double(all[loop].took.count()) * weight;
		return sum;
	}
};
