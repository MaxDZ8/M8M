/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <vector>
#include <string>
#include "../BTC/Funcs.h"
#include "../BTC/Structs.h"
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

struct WUCoinbaseDesc { //!< contains data relative to coinbase and how to mangle it to the header, thus including merkle root tree
	std::vector<aubyte> binary; //!< A coinbase obtained from server state with nonce2=0 at a rectain offset.
	asizei nonceTwoOff;
	std::vector<btc::MerkleRoot> merkles;
	asizei merkleOff; //!< where in the header to put hashed (merkle tree + cb)
	WUCoinbaseDesc() : nonceTwoOff(0), merkleOff(0) { }
};


/*! This was originally a structure holding everything needed to go on hashing.
However, various coins generate those values differently and when it comes to nonce2 rolling, this is important.
It seems nice to not bother the network IO/server manager just to nonce2 roll so this was upgraded to a more generic system.

Additionally, some algos allow to skip some computation in the beginning, usually being common to the first 512-bit (uint16) chunk of data
to hash, being common to all scan-hash instances. This thing is (perhaps inappropriately) called "midstate"

Those new work units encapsulate only "starting state" and provide ways to generate rolled work units internally. */
struct AbstractWorkUnit : public WUJobInfo, WUDifficulty {
public:
	WUCoinbaseDesc coinbase;
	int ntime;
	time_t genTime;
	auint nonce2;
	bool restart; //!< if this is false then keep the nonce2 you're already iterating, but with the new data.
	std::array<aubyte, 128> header; //!< Updated by calling MakeNoncedHeader, call this when a new nonce is set.
	double networkDiff; 
	
	AbstractWorkUnit(const WUJobInfo &family, int networkTime, const WUDifficulty &diff, const std::array<aubyte, 128> &startHeader)
		: WUJobInfo(family), WUDifficulty(diff), ntime(networkTime), blankHeader(startHeader), genTime(time(NULL)), nonce2(0), restart(false) {
		networkDiff = std::numeric_limits<double>::infinity();
			/* OBSOLETE and no more used. This is the midstate for scrypt1024.
		// Automatically compute the 'midstate'
		aubyte flipped[64];
		hashing::BTCSHA256 mangle;
		hashing::BTCSHA256::Digest temp;
		btc::FlipIntegerBytes<16>(flipped, data.data());
		mangle.BlockProcessing(flipped);
		memcpy_s(midstate.data(), sizeof(midstate), mangle.GetHashLE(temp).data(), sizeof(temp));
		btc::FlipBytesIFBE(midstate.data(), midstate.data() + 32);
		*/
#if defined(_DEBUG)
		memset(header.data(), 0, sizeof(header));
#endif
	}
	explicit AbstractWorkUnit() : ntime(0), genTime(0) { }
	virtual ~AbstractWorkUnit() { }

	virtual void MakeCBMerkle(std::array<aubyte, 32> &initialMerkle) const = 0;

	void MakeNoncedHeader(bool littleEndianAlgo) {
		std::array<aubyte, 32> merkleRoot;
		MakeCBMerkle(merkleRoot);
		std::array<aubyte, 64> merkleSHA;
		std::copy(merkleRoot.cbegin(), merkleRoot.cend(), merkleSHA.begin());
		for(asizei loop = 0; loop < coinbase.merkles.size(); loop++) {
			auto &sign(coinbase.merkles[loop].hash);
			std::copy(sign.cbegin(), sign.cend(), merkleSHA.begin() + 32);
			btc::SHA256Based(DestinationStream(merkleRoot.data(), sizeof(merkleRoot)), merkleSHA);
			std::copy(merkleRoot.cbegin(), merkleRoot.cend(), merkleSHA.begin());
		}
		// vvv I tried to do that using std::copy, but I hate it.
		if(littleEndianAlgo) memcpy_s(merkleRoot.data(), sizeof(merkleRoot), merkleSHA.data(), sizeof(merkleRoot));
		else btc::FlipIntegerBytes<8>(merkleRoot.data(), merkleSHA.data()); // most of the time
		
		aubyte *raw = header.data();
		memcpy_s(raw, 128, blankHeader.data(), sizeof(blankHeader));
		raw += coinbase.merkleOff;
		memcpy_s(raw, 128 - coinbase.merkleOff, merkleRoot.data(), sizeof(merkleRoot));
		
		if(littleEndianAlgo) { // the structure is the same but several bytes must be flipped.
			raw = header.data();
			for(auint shuffle = 0; shuffle < coinbase.merkleOff; shuffle += 4) {
				aubyte load[4];
				for(auint i = 0; i < 4; i++) load[i] = raw[shuffle + i];
				for(auint i = 0; i < 4; i++) raw[shuffle + 3 - i] = load[i];
			}
			// merkle is already in the right layout
			// nbits, ntime
			for(auint shuffle = 68; shuffle < 76; shuffle += 4) {
				aubyte load[4];
				for(auint i = 0; i < 4; i++) load[i] = raw[shuffle + i];
				for(auint i = 0; i < 4; i++) raw[shuffle + 3 - i] = load[i];
			}
		}
	}

	//! \todo This is valid only after MakeNoncedHeader has been called... perhaps it should be updated here... once?
	double ExtractNetworkDiff(bool littleEndianAlgo, aulong algoDiffNumerator) {
		// This looks like share difficulty calculation... but not quite.
		aulong diff;
		double numerator;
		const auint *src = reinterpret_cast<const auint*>(header.data() + 72);
		if(littleEndianAlgo) {
			aulong blob = BETOH(*src) & 0xFFFFFF00;
			blob <<= 8;
			blob = 
			diff = SWAP_BYTES(blob);
			numerator = static_cast<double>(algoDiffNumerator);
		}
		else {
			aubyte pow = header[72];
			aint powDiff = (8 * (0x1d - 3)) - (8 * (pow - 3)); // don't ask.
			diff = BETOH(*src) & 0x0000000000FFFFFF;
			numerator = static_cast<double>(algoDiffNumerator << powDiff);
		}
		if(diff == 0) diff = 1;
		networkDiff = numerator / double(diff);
		return networkDiff;
	}

protected:
	std::array<aubyte, 128> blankHeader;
	std::array<aubyte, 32> midstate;
};

}
