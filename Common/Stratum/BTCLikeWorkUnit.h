/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractWorkUnit.h"

namespace stratum {

class DoubleSHA2WorkUnit : public AbstractWorkUnit {
	void MakeCBMerkle(std::array<aubyte, 32> &imerkle) const {
		btc::SHA256Based(imerkle, coinbase.binary.data(), coinbase.binary.size());
	}
public:
	DoubleSHA2WorkUnit(const WUJobInfo &family, int networkTime, const WUDifficulty &diff, const std::array<aubyte, 128> &blankHeader)
		: AbstractWorkUnit(family, networkTime, diff, blankHeader) { }
};

class SingleSHA2WorkUnit : public AbstractWorkUnit {
	void MakeCBMerkle(std::array<aubyte, 32> &imerkle) const {
		using hashing::BTCSHA256;
		BTCSHA256 hasher(BTCSHA256(coinbase.binary.data(), coinbase.binary.size()));
		hasher.GetHash(imerkle);
	}
public:
	SingleSHA2WorkUnit(const WUJobInfo &family, int networkTime, const WUDifficulty &diff, const std::array<aubyte, 128> &blankHeader)
		: AbstractWorkUnit(family, networkTime, diff, blankHeader) { }
};


}