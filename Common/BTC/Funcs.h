/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <iterator>
#include <vector>
#include <array>
#include "../AREN/SerializationBuffers.h"
#include "../hashing.h"

namespace btc {

/*! Legacy miners go around with a file titled "FIPS 180-2 SHA-224/256/384/512 implementation".
It is actually an hash based on SHA256, and very far from being a conforming implementation.
Maybe it was conforming in the past, but not after the optimizations involving the fact that it is used to hash small messages.
This tries to mimic the results of that strange function often erroneously called "sha256".
The legacy function is really, really wrong and does not seem to take advantage of the fact the message is guaranteed to be 960 bits.
In fact it is then used to SHA-256 a SHA256 digest so it's basically two cases here, either one or two blocks to mangle.
In the end, I don't care and use an implementation which is (hopefully) fully confarmant. */
void SHA256Based(DestinationStream &storage, const aubyte *msg, const asizei count);
void SHA256Based(std::array<aubyte, 32> &storage, const aubyte *msg, const asizei count);

template<asizei SZ>
void SHA256Based(std::array<aubyte, 32> &storage, const std::array<aubyte, SZ> &msg) {
	DestinationStream dst(storage.data(), sizeof(storage));
	return SHA256Based(dst, msg.data(), sizeof(msg));
}

template<asizei SZ>
void SHA256Based(DestinationStream &storage, const std::array<aubyte, SZ> &msg) {
	return SHA256Based(storage, msg.data(), sizeof(msg));
}


aushort ByteSwap(aushort value);
auint ByteSwap(auint value);
aulong ByteSwap(aulong value);
void FlipBytes(aubyte *begin, aubyte *end);

template<asizei BYTES>
void CopyFlippingBytes(aubyte *dst, const aubyte *src) {
	src += BYTES - 1;
	for(asizei loop = 0; loop < BYTES; loop++) {
		*dst = *src;
		dst++;
		src--;
	}
}


//! Generic version, supports unaligned memory.
//! This maintains word order but flips the bytes of each word.
//! \sa FlipBytes
template<asizei COUNT>
aubyte* FlipIntegerBytes(aubyte *dst, const aubyte *src) {
	for(asizei loop = 0; loop < COUNT ; loop++) {
		asizei off = loop * sizeof(auint);
		auint s;
		memcpy_s(&s, sizeof(s), src + off, sizeof(s));
		s = ByteSwap(s);
		memcpy_s(dst + off, sizeof(auint) * COUNT - off, &s, sizeof(s));
	}
	return dst;
}


//! Generic version, supports unaligned memory.
//! dst is src flipped, reversed order.
template<asizei BYTES>
aubyte* FlipBytes(aubyte *dst, aubyte *src) {
	for(asizei loop = 0; loop * sizeof(auint) < BYTES ; loop++) {
		auint s;
		memcpy_s(&s, sizeof(s), src + loop * sizeof(auint), sizeof(auint));
		s = ByteSwap(s);
		asizei dstOff = loop * sizeof(auint);
		memcpy_s(dst + dstOff, BYTES - dstOff, &s, sizeof(s));
	}
	return dst;
}


void FlipBytesIFBE(aubyte *begin, aubyte *end);


template<asizei BYTES>
void CopyFlippingBytesTOLE(aubyte *dst, const aubyte *src) {
#if defined(_M_AMD64) || defined _M_IX86
	src += BYTES - 1;
	for(asizei loop = 0; loop < BYTES; loop++) {
		*dst = *src;
		dst++;
		src--;
	}
#else
#error CopyFlippingBytesTOLE needs some care
#endif
}


/*! Build the "target string", a more accurate representation of the real 256-bit difficulty.
In the beginning this was a LTC miner and had an hardcoded trueDiffOne multiplier of 64*1024, as LTC has.
Now this is no more the case and the trueDiffOne multiplier must be passed from outer decisions. It does not really affect computation but
it's part of the stratum state as it's a function of coin being mined. */
std::array<aulong, 4> MakeTargetBits(adouble diff, adouble diffOneMul);

//! This is more important than other magic numbers regarding difficulty as it's used to determine share/nonce difficulty value.
static const adouble TRUE_DIFF_ONE = 26959535291011309493156476344723991336010898738574164086137773096960.0;


//! It's basically the inverse of MakeTargetBits
adouble LEToDouble(const std::array<aulong, 4> &target);


}


/*! A slightly modified / simplified form of target calculation, premiered with NeoScrypt.
It is, at its core, more or less the same thing: dividing by power-of-two so it keeps being precise on progressive divisions. */
std::array<aulong, 4> MakeTargetBits_NeoScrypt(adouble diff, adouble diffOneMul);
