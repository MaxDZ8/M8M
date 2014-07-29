/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <vector>
#include <string>
#include "../BTC/Funcs.h"
#include <time.h>

namespace stratum {

struct WUJobInfo {
	std::vector<aubyte> nonceOne;
	std::string job;
	WUJobInfo(const std::vector<aubyte> &nonce, const std::string &jobID) : nonceOne(nonce), job(jobID) { }
	explicit WUJobInfo() { }
	bool operator!=(const WUJobInfo &other) const {
		return nonceOne != other.nonceOne || job != other.job;
	}
};

struct WUDifficulty {
	double shareDiff;
	std::array<aulong, 4> target;
	WUDifficulty(double sdiff, const std::array<aulong, 4> &target256) : shareDiff(sdiff), target(target256) { }
	explicit WUDifficulty() { }
	bool operator!=(const WUDifficulty &other) const {
		return shareDiff != other.shareDiff || target != other.target;
	}
};


struct WorkUnit : public WUJobInfo, WUDifficulty {
	int ntime;
	std::array<aubyte, 128> header;
	std::array<aubyte, 32> midstate;
	time_t genTime;
	auint nonce2; //!< nonce2 incapsulated in this header. Redundant with header data, but easy to go
	
	WorkUnit(const WUJobInfo &family, int networkTime, const WUDifficulty &diff, const std::array<aubyte, 128> &data, auint nonce2InHeader)
		: WUJobInfo(family), WUDifficulty(diff), ntime(networkTime), header(data), genTime(time(NULL)), nonce2(nonce2InHeader) {
		// Automatically compute the 'midstate'
		aubyte flipped[64];
		hashing::BTCSHA256 mangle;
		hashing::BTCSHA256::Digest temp;
		btc::FlipIntegerBytes<16>(flipped, data.data());
		mangle.BlockProcessing(flipped);
		memcpy_s(midstate.data(), sizeof(midstate), mangle.GetHashLE(temp).data(), sizeof(temp));
		btc::FlipBytesIFBE(midstate.data(), midstate.data() + 32);
	}
	explicit WorkUnit() : ntime(0), genTime(0) { }
	bool operator!=(const WorkUnit &other) const {
		if(other.ntime != ntime) return true;
		if(other.genTime != genTime) return true;
		if(other.header != header) return true;
		if(other.midstate != midstate) return true;
		if(static_cast<const WUJobInfo&>(*this) != static_cast<const WUJobInfo&>(other)) return true;
		return static_cast<const WUDifficulty&>(*this) != static_cast<const WUDifficulty&>(other);
	}
	bool operator==(const WorkUnit &other) const { return !(other != *this); }
};

}
