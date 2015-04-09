/*
 * Copyright (C) 2015 Massimo Del Zotto
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/AREN/ArenDataTypes.h"
#include <string>
#include <vector>
#include <array>

/*! This structure uniquely identify the source of a header so results can be passed back to the right source.
The source itself is identified by a void* to avoid dependancy on the specific structure, which is bad anyway as
those could be accessed asynchronously and I don't want to encourage that.
Inside each source, there will be various jobs over time so we have to identify that as well.

Nonces are not really "originated" from the data associated with this structure but rather originated from the mining process
started from the data itself. */
struct NonceOriginIdentifier {
    const void *owner;
    std::string job;
    explicit NonceOriginIdentifier() : owner(nullptr) { }
    NonceOriginIdentifier(const void *from, const char *j) : owner(from), job(j) { }
    NonceOriginIdentifier(const void *from, const std::string &j) : NonceOriginIdentifier(from, j.c_str()) { }
};

/*! Mining algorithms take an header and produce nonces. The mining process must keep track of a value often referred as "nonce2",
which can/must be rolled every time the nonce range is exhausted. Nonce2 is required to produce a valid result. Nonce2 however is embedded in the header
and mining algorithms don't care about it. This structure is used by mining algorithms to give back nonce values to the mining process manager, which
will recostruct the nonce2 used. */
struct MinedNonces {
    std::array<aubyte, 80> from;
    std::vector<auint> nonces;
    std::vector<auint> hashes; //!< hashes[i] is the hash produced by nonces[i], so I can test computation is correct.
    explicit MinedNonces() = default;
    MinedNonces(const std::array<aubyte, 80> &hashOriginator) : from(hashOriginator) { }
};


/*! Nonces produced by a certain device and passed out to control/send thread.
The fact nonces were found does not imply they are going to be sent... nor there is something to send.
Nonces might be discarded for being under target or hardware miscomputing. */
struct VerifiedNonces {
    asizei discarded; //!< those nonces were valid but won't be returned as below target, would get rejected. We have been unlucky.
    asizei wrong; //!< those nonces produce hashes not matching across GPU and CPU validation. Also called "HW" error. Most likely not a transient error.
    auint nonce2; //!< common to all nonces, assuming nonces.length() > 0, otherwise undefined
    struct Nonce {
        auint nonce; //!< the magic number to send
        std::array<aubyte, 4> hashSlice; //!< slice of the produced hash, for feedback when legacy compatibility requested
        adouble diff; //!< difficulty of this specific share, again for feedback mostly
        bool block; //!< true if this exceeds network difficulty and thus solved a block
        Nonce() : nonce(0), diff(.0), block(false) { }
    };
    std::vector<Nonce> nonces;
    asizei device; //!< device which produced the nonces for running statistics
    adouble targetDiff; //!< target diff used for the scan which produced this set of nonces.
    VerifiedNonces() : discarded(0), wrong(0) { }
    asizei Total() const { return discarded + wrong + nonces.size(); }
};
