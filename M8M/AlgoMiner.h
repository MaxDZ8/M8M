/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "ThreadedNonceFinders.h"

template<typename BlockVerifier>
class AlgoMiner : public ThreadedNonceFinders {
public:
    AlgoMiner(std::function<void(auint)> sleepFunc) : ThreadedNonceFinders(sleepFunc) { }

private:
    std::array<aubyte, 32> HashHeader(std::array<aubyte, 80> &header, auint nonce) const {
        BlockVerifier checker;
        return checker.Hash(header, nonce);
    }
};
