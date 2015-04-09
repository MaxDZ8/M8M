#pragma once
#include "../Common/AREN/ArenDataTypes.h"
#include <array>

/*! Those are basically functors and expected to be used directly, even though they use virtual functions.
I still go for classes so I can have some internal state.
Always remember this is called every time a nonce is FOUND, which is a few times per minute so not really a performance path. */
class BlockVerifierInterface {
public:
    virtual ~BlockVerifierInterface() { }
    //! The hash must be in the same byte layout as btc::LEToDouble
    //! The nonce must be the value returned by the GPU kernel.
    virtual std::array<aubyte, 32> Hash(std::array<aubyte, 80> baseBlockHeader, auint nonce) = 0;
};
