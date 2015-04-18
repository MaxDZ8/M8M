/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
/*! \file Multiple structures regarding the way stratum pools generate work an pass it around. The main deal is Stratum pools generate two data streams:
1- Effective work block headers
2- Difficulty adjustments. */
#include <array>
#include "../AREN/ArenDataTypes.h"


namespace stratum {

/*! On pooled mining, work difficulty is just as important as the block header as the server will refuse work easier than requested.
In terms of the BTC protocol, only the 256bit "target" number is important but stratum works on difficulty instead. It is a widespread convention.
In theory one implies the other and I could easily convert but for laziness, I don't. */
struct WorkDiff {
	double shareDiff;
	std::array<aulong, 4> target;
	WorkDiff(double sdiff, const std::array<aulong, 4> &target256) : shareDiff(sdiff), target(target256) { }
    explicit WorkDiff() : shareDiff(.0) { for(auto &init : target) init = 0; }
    bool operator!=(const WorkDiff &other) const { return target != other.target; }
    bool Valid() const { return shareDiff != .0; }
    bool operator!() const { return !Valid(); }
};


//! Work is basically an header to be iterated 4^32 times. 
//! However, to correctly reply we have to keep track of the originating nonce2 and ntime used.
struct Work {
    std::array<aubyte, 128> header; //!< only first 80 bytes are hashed but I keep it anyway
    auint nonce2, ntime;
    std::string job; //!< The originating AbstractWorkSource is tracked by other means.
};


/*! Legacy miners have "work units" (type "work") floating around. Similarly, I had pools producing work units.
This was a bit unconvenient as work units have to be produced miner-side on need for "nonce2 rolling".
There is also the need to provide an easy way to do "ntime rolling" in the future. For the time being, this is not there, ntime is embedded in the basic
header as soon as notify is received so I'll have to think at this in more detail.
So, WorkSources will now generate factory objects whose goal is to build an header to hash. */
class AbstractWorkFactory {
public:
    typedef std::function<void(std::array<aubyte, 32> &merkleOut, const std::vector<aubyte> &coinbase)> CBHashFunc;
    AbstractWorkFactory(bool restartWork, auint networkTime, const CBHashFunc cbmode, const std::string &poolJob)
        : ntime(networkTime), initialMerkle(cbmode), job(poolJob), restart(restartWork) { }
    virtual ~AbstractWorkFactory() { }
    const std::string job;
    const bool restart; //!< if false, take nonce2 from previous factory, if any, call Continuing before anything else

    void Continuing(const AbstractWorkFactory &previous) { nonce2 = previous.nonce2; }

    Work MakeNoncedHeader(bool littleEndianAlgo, aulong algoDiffNumerator) {
        Work result;
        const asizei rem = coinbase.size() - nonceTwoOff;
        const auint nonce2BE = HTON(nonce2);
	    memcpy_s(coinbase.data() + nonceTwoOff, rem, &nonce2BE, sizeof(nonce2BE));
        result.nonce2 = nonce2++;
        result.ntime = ntime;
        result.job = job;
		std::array<aubyte, 32> merkleRoot;
		initialMerkle(merkleRoot, coinbase);
		std::array<aubyte, 64> merkleSHA;
		std::copy(merkleRoot.cbegin(), merkleRoot.cend(), merkleSHA.begin());
		for(asizei loop = 0; loop < merkles.size(); loop++) {
			auto &sign(merkles[loop]);
			std::copy(sign.cbegin(), sign.cend(), merkleSHA.begin() + 32);
			btc::SHA256Based(DestinationStream(merkleRoot.data(), sizeof(merkleRoot)), merkleSHA);
			std::copy(merkleRoot.cbegin(), merkleRoot.cend(), merkleSHA.begin());
		}
		// vvv I tried to do that using std::copy, but I hate it.
		if(littleEndianAlgo) memcpy_s(merkleRoot.data(), sizeof(merkleRoot), merkleSHA.data(), sizeof(merkleRoot));
		else btc::FlipIntegerBytes<8>(merkleRoot.data(), merkleSHA.data()); // most of the time
		
        result.header = blankHeader;
		aubyte *raw = result.header.data() + merkleOff;
		memcpy_s(raw, 128 - merkleOff, merkleRoot.data(), sizeof(merkleRoot));

		if(littleEndianAlgo) { // the structure is the same but several bytes must be flipped.
			raw = result.header.data();
			for(auint shuffle = 0; shuffle < merkleOff; shuffle += 4) {
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
        return result;
	}
    virtual double GetNetworkDiff() const = 0;

protected:
    auint nonce2 = 0;
    asizei nonceTwoOff;
    auint ntime;
    std::vector<aubyte> coinbase; //!< binary, nonce2 is to be put there at a certain offset specified below.
    CBHashFunc initialMerkle; //!< after nonce2 is slapped in coinbase, this function is called to hash it giving an initial merkle root
    asizei merkleOff;
    std::array<aubyte, 128> blankHeader;
    std::vector<std::array<aubyte, 32>> merkles;
};


}
