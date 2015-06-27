/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "ThreadedNonceFinders.h"
#include "../BlockVerifiers/BlockVerifierInterface.h"

class AlgoMiner : public ThreadedNonceFinders {
public:
    AlgoMiner(BlockVerifierInterface &bv) : checker(bv) { }
private:
    BlockVerifierInterface &checker;
    std::array<aubyte, 32> HashHeader(std::array<aubyte, 80> &header, auint nonce) const {
        return checker.Hash(header, nonce);
    }
};
