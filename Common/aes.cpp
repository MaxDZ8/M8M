/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "aes.h"

namespace aes {


aubyte* StoreMultiplicativeInverses(aubyte *inv) {
	// I could store them directly but I don't really need them besides temporary use.
	// The paper says I should compute them using the "Extended Euclid Algorithm" but I don't care.
	// I just brute force it.
	inv[0] = 0;
	for(unsigned a = 1; a < 256; a++) {
		for(unsigned b = 0; b < 256; b++) {
			if(poli::mul(static_cast<unsigned char>(a), static_cast<unsigned char>(b)) == 0x01) inv[a] = static_cast<unsigned char>(b);
		}
	}
	return inv;
}


auint* RoundTableRowZero(auint *lut) {
	auto pack = [](aubyte a, aubyte b, aubyte c, aubyte d) -> auint {
		aushort ab = aushort(a) << 8 | b;
		aushort cd = aushort(c) << 8 | d;
		return ab << 16 | cd;
	};
	for(int i = 0; i < 256; i++) {
		const aubyte v(sbox[i]);
		lut[i] = pack(poli::mul(v, 0x03), v, v, poli::mul(v, 0x02));
	}
	return lut;
};


namespace poli {

aubyte mod_aes(unsigned int remainder) {
	auto highestOne = [](unsigned short bin) -> unsigned char {
		for(unsigned int i = sizeof(bin) * 8 - 1; i < sizeof(bin) * 8; i--) {
			if((bin & (1 << i)) != 0) return i;
		}
		return 0;
	};

	const unsigned short divisor = 0x011B;
	const unsigned short divDegree = highestOne(divisor);
	unsigned int quotient = 0;
	while(remainder) {
		const unsigned short greater = highestOne(remainder);
		if(greater < divDegree) break;
		// now to multiply the divisor polynomial by x^something I just have to to shift it...
		const unsigned short qpartial = greater - divDegree;
		const unsigned short sub = divisor << qpartial; // operator+ is the same as operator- in modulo2 polynomial as operator+ is XOR
		remainder ^= sub;
	}
	return remainder;
}


aubyte mul(unsigned char a, unsigned char b) {
	unsigned int mangled = 0;
	for(unsigned int bit = 0; bit < 8; bit++) {
		if(b & (1 << bit)) {
			mangled ^= a << bit;
		}
	}
	return mod_aes(mangled);
}


}
}
