/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "Funcs.h"


namespace btc {

void SHA256Based(DestinationStream &storage, const aubyte *msg, const asizei count) {
	using hashing::BTCSHA256;
	BTCSHA256 hasher(BTCSHA256(msg, count), true);
	hasher.GetHash(storage);
}

void SHA256Based(std::array<aubyte, 32> &storage, const aubyte *msg, const asizei count) {
	DestinationStream dst(storage.data(), sizeof(storage));
	return SHA256Based(dst, msg, count);
}

aushort ByteSwap(aushort value) { return value >> 8 | ((value & 0x00FF) << 8); }
auint ByteSwap(auint value) { 
	auint hi =  ByteSwap(aushort(value & 0x0000FFFF)) << 16;
	auint low = ByteSwap(aushort(value >> 16));
	return hi | low;
}
aulong ByteSwap(aulong value) {
	aulong hi =  static_cast<aulong>(ByteSwap(auint(value & 0x00000000FFFFFFFF))) << 32;
	aulong low = ByteSwap(auint(value >> 32));
	return hi | low;
}
void FlipBytes(aubyte *begin, aubyte *end) {
	end--;
	while(begin < end) {
		std::swap(*begin, *end);
		begin++;
		end--;
	}
}


void FlipBytesIFBE(aubyte *begin, aubyte *end) { 
#if defined(_M_AMD64) || defined _M_IX86
	// nothing to do on natively little endian architectures.
#else
	FlipBytes(begin, end);
#endif
}


adouble LEToDouble(const std::array<aulong, 4> &target) {
	const adouble TRUE_DIFF_ONE = 26959535291011309493156476344723991336010898738574164086137773096960.0;
	const adouble BITS_192 = 6277101735386680763835789423207666416102355444464034512896.0;
	const adouble BITS_128 = 340282366920938463463374607431768211456.0;
	const adouble BITS_64 = 18446744073709551616.0;
	adouble ret = 0;
	const adouble k[4] = { BITS_192, BITS_128, BITS_64, 1 };
	for(asizei loop = 0; loop < 4; loop++) {
		aulong integer = target[3 - loop];
		ret += LETOH(integer) * k[loop];
	}	
	return ret;

}


}
