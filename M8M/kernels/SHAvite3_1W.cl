/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
/*! Differently from other implementations, SHAvite3 is not "transversal" across different
nearby WI. Why?
Lib SPH implementation might be a bit hard to read but it's almost optimal.
It allows to resolve registers directly inside each WI so basically all values can be accessed
at no cost. By contrast, using LDS to share data mandates use of at least some computation to
access the very same values, as they have to be discriminated across different hashes.
The very small benefit of running 4-way is soon cancelled by the extra computation to access
the correct LDS memory location.

As such, SHAvite3 implementation is monolithic just as SPH is. SPH basically unrolls everything
ending up quite long and explicit on the details but losing the structure.

I try to emphatize a bit more on structure by running everything on compile-time constants
and letting the compiler decide what to do. */


// SHAvite3 is based on the AES rounds. Note this is slightly different from the ones used in ECHO.
void AESRound(uint *val, uint k0, uint k1, uint k2, uint k3,
              local uint *lut0, local uint *lut1, local uint *lut2, local uint *lut3) {
#if __ENDIAN_LITTLE__
#define LUT(li, val)  (lut##li != 0? lut##li[(val >> (8 * li)) & 0xFF] : rotate(lut0[(val >> (8 * li)) & 0xFF], (8u * li##u)))
	uint i0 = val[0];
	uint i1 = val[1];
	uint i2 = val[2];
	uint i3 = val[3];
	val[0] = lut0[i0 & 0xFF] ^ LUT(1, i1) ^ LUT(2, i2) ^ LUT(3, i3) ^ k0;
	val[1] = lut0[i1 & 0xFF] ^ LUT(1, i2) ^ LUT(2, i3) ^ LUT(3, i0) ^ k1;
	val[2] = lut0[i2 & 0xFF] ^ LUT(1, i3) ^ LUT(2, i0) ^ LUT(3, i1) ^ k2;
	val[3] = lut0[i3 & 0xFF] ^ LUT(1, i0) ^ LUT(2, i1) ^ LUT(3, i2) ^ k3;
#undef LUT
#else
#error Endianness?
#endif
}


void AESRoundNoKey(uint *val,
                   local uint *lut0, local uint *lut1, local uint *lut2, local uint *lut3) {
	AESRound(val, 0, 0, 0, 0, lut0, lut1, lut2, lut3);
}


// SHAvite3 is based on the AES rounds. Note this is slightly different from the ones used in ECHO.
void AESRoundLDS(local uint *val, uint k0, uint k1, uint k2, uint k3,
              local uint *lut0, local uint *lut1, local uint *lut2, local uint *lut3) {
#if __ENDIAN_LITTLE__
#define LUT(li, val)  (lut##li != 0? lut##li[(val >> (8 * li)) & 0xFF] : rotate(lut0[(val >> (8 * li)) & 0xFF], (8u * li##u)))
	uint i0 = val[0 * 64];
	uint i1 = val[1 * 64];
	uint i2 = val[2 * 64];
	uint i3 = val[3 * 64];
	val[0 * 64] = lut0[i0 & 0xFF] ^ LUT(1, i1) ^ LUT(2, i2) ^ LUT(3, i3) ^ k0;
	val[1 * 64] = lut0[i1 & 0xFF] ^ LUT(1, i2) ^ LUT(2, i3) ^ LUT(3, i0) ^ k1;
	val[2 * 64] = lut0[i2 & 0xFF] ^ LUT(1, i3) ^ LUT(2, i0) ^ LUT(3, i1) ^ k2;
	val[3 * 64] = lut0[i3 & 0xFF] ^ LUT(1, i0) ^ LUT(2, i1) ^ LUT(3, i2) ^ k3;
#undef LUT
#else
#error Endianness?
#endif
}


void AESRoundNoKeyLDS(local uint *val,
                   local uint *lut0, local uint *lut1, local uint *lut2, local uint *lut3) {
	AESRoundLDS(val, 0, 0, 0, 0, lut0, lut1, lut2, lut3);
}




/*! Odd things. If you look at SHAvite3 documentation, the IV vector should have different values.
Those are the magic numbers from SPHLIB 3 and I just have to match them.
Perhaps they are part of the "anticipated" corrections by SPHLIB to the specification (?).*/
static constant uint SHAvite3_512_IV[4][4] = {
    { 0x72FCCDD8u, 0x79CA4727u, 0x128A077Bu, 0x40D55AECu },
    { 0xD1901A06u, 0x430AE307u, 0xB29F5CD1u, 0xDF07FBFCu },
    { 0x8E45D73Du, 0x681AB538u, 0xBDE86578u, 0xDD577E47u },
    { 0xE275EADEu, 0x502D9FCDu, 0xB9357178u, 0x022A4B9Au }
};


/*! See kernel code for more documentation about that. It's basically a precomputed message pad
for a 512bit message. */
static constant uint SHAvite3_512_precomputedPadding[4][4] = {
	{ 0x00000080, 0x00000000, 0x00000000, 0x00000000 }, 
	{ 0x00000000, 0x00000000, 0x00000000, 0x00000000 }, 
	{ 0x00000000, 0x00000000, 0x00000000, 0x02000000 }, 
	{ 0x00000000, 0x00000000, 0x00000000, 0x02000000 }
};


void RotateRegisters(uint *v) {
	uint temp = v[0];
	v[0] = v[1];
	v[1] = v[2];
	v[2] = v[3];
	v[3] = temp;
}


uint RKSelect(uint i, bool oddRound, uint alternating, uint *main, uint *other) {
	const uint distance = oddRound? 12 : 9;
	uint get = i + distance;
	// distance 12 is the easiest, I just have to wrap around the index.
	if(distance == 12) {
		get %= 16;
		return alternating? other[get] : main[get];
	}
	// distance 9 is a bit more complicated as the last index +3 might need to be
	// wrapped-around to the other array no matter what.
	bool overflow = alternating && get >= 16;
	get %= 16;
	return alternating ^ overflow? other[get] : main[get];
}


void SHAvite3_Step(uint roundix, uint stepix, local uint *x, uint *rko, uint *rki, local uint *aeslut0, local uint *aeslut1, local uint *aeslut2, local uint *aeslut3) {
	uint *mainrk = stepix < 4? rko : rki;
	const uint base = (stepix % 4) * 4;
	
	if(roundix) { // nonzero rounds modify mainrk values before using them.
		const bool oddRound = roundix % 2;
		if(oddRound) {
			AESRoundNoKey(mainrk + base, aeslut0, aeslut1, aeslut2, aeslut3);
			RotateRegisters(mainrk + base);
		}
		
		// Also modify round keys before use by xorring them with other round keys. The way the other keys
		// are selected is not trivial. As first approximation, odd rounds xor with RK at +12 distance (%16)
		// while even rounds use RK +9 distance. Some steps in even rounds will therefore overflow across rkX
		// registers. Furthermore, odd and even round steps take elements from different positions: in general
		// all (steps%4) == 0 in a round will take rko ^= rki, (steps%4)==1 will do rko ^= rko for odd rounds
		// and rko ^= rki for even rounds! Other two steps are always coherent, either rko ^= rko or rki ^= rki,
		// which is the same thing as I swap them.
		const bool alternating = (stepix % 4) < (oddRound? 1 : 2);
		uint *otherrk = stepix < 4? rki : rko;
		mainrk[base + 0] ^= RKSelect(base + 0, oddRound, alternating, mainrk, otherrk);
		mainrk[base + 1] ^= RKSelect(base + 1, oddRound, alternating, mainrk, otherrk);
		mainrk[base + 2] ^= RKSelect(base + 2, oddRound, alternating, mainrk, otherrk);
		mainrk[base + 3] ^= RKSelect(base + 3, oddRound, alternating, mainrk, otherrk);
	}
	// If the above is not enough, some steps also mixin a bit counter. In normal SHAvite3 this is updated
	// in various compression function steps but since miners deal with short messages, this is a constant.
	uint mixin = 0;
	mixin += roundix ==  1 && stepix == 0? 1 : 0;
	mixin += roundix ==  5 && stepix == 1? 2 : 0;
	mixin += roundix ==  9 && stepix == 7? 3 : 0;
	mixin += roundix == 13 && stepix == 6? 4 : 0;
	if(mixin) {
		uint counter[4] = { 512, 0, 0, 0 };
		switch(mixin) {
		case 1:
			mainrk[base + 0] ^=  counter[0];
			mainrk[base + 1] ^=  counter[1];
			mainrk[base + 2] ^=  counter[2];
			mainrk[base + 3] ^= ~counter[3];
			break;
		case 2:
			mainrk[base + 0] ^=  counter[3];
			mainrk[base + 1] ^=  counter[2];
			mainrk[base + 2] ^=  counter[1];
			mainrk[base + 3] ^= ~counter[0];
			break;
		case 3:
			mainrk[base + 0] ^=  counter[2];
			mainrk[base + 1] ^=  counter[3];
			mainrk[base + 2] ^=  counter[0];
			mainrk[base + 3] ^= ~counter[1];
			break;
		case 4:
			mainrk[base + 0] ^=  counter[1];
			mainrk[base + 1] ^=  counter[0];
			mainrk[base + 2] ^=  counter[3];
			mainrk[base + 3] ^= ~counter[2];
			break;
		}
	}
	for(uint i = 0; i < 4; i++) x[64 * i] ^= mainrk[base + i];
	AESRoundNoKeyLDS(x, aeslut0, aeslut1, aeslut2, aeslut3);
}


// Make sure roundix is a compile time constant as registers cannot be accessed using relative addressing!
void SHAvite3_Round(uint roundix, uint *p, uint *rko, uint *rki, local uint *x, local uint *aeslut0, local uint *aeslut1, local uint *aeslut2, local uint *aeslut3) {
	uint src = (((roundix % 4) / 2) * 2 + (roundix % 2? 0 : 1)) * 4;
	uint dst = (src + 12) % 16;
	x += get_local_id(0);
	{
		//uint x[4] = { p[src + 0], p[src + 1], p[src + 2], p[src + 3] };
		x[0 * 64] = p[src + 0];
		x[1 * 64] = p[src + 1];
		x[2 * 64] = p[src + 2];
		x[3 * 64] = p[src + 3];
		SHAvite3_Step(roundix, 0, x, rko, rki, aeslut0, aeslut1, aeslut2, aeslut3);
		SHAvite3_Step(roundix, 1, x, rko, rki, aeslut0, aeslut1, aeslut2, aeslut3);
		SHAvite3_Step(roundix, 2, x, rko, rki, aeslut0, aeslut1, aeslut2, aeslut3);
		SHAvite3_Step(roundix, 3, x, rko, rki, aeslut0, aeslut1, aeslut2, aeslut3);
		for(uint i = 0; i < 4; i++) p[dst + i] ^= x[i * 64];
	}
	src += 8;
	src %= 16;
	dst += 8;
	dst %= 16;
	{
		//uint x[4] = { p[src + 0], p[src + 1], p[src + 2], p[src + 3] };
		x[0 * 64] = p[src + 0];
		x[1 * 64] = p[src + 1];
		x[2 * 64] = p[src + 2];
		x[3 * 64] = p[src + 3];
		SHAvite3_Step(roundix, 4, x, rko, rki, aeslut0, aeslut1, aeslut2, aeslut3);
		SHAvite3_Step(roundix, 5, x, rko, rki, aeslut0, aeslut1, aeslut2, aeslut3);
		SHAvite3_Step(roundix, 6, x, rko, rki, aeslut0, aeslut1, aeslut2, aeslut3);
		SHAvite3_Step(roundix, 7, x, rko, rki, aeslut0, aeslut1, aeslut2, aeslut3);
		for(uint i = 0; i < 4; i++) p[dst + i] ^= x[i * 64];
	}
}


__attribute__((reqd_work_group_size(64, 1, 1)))
kernel void SHAvite3_1way(global uint *input, global uint *hashOut, global uint *aes_round_luts) {
	input   += (get_global_id(0) - get_global_offset(0)) * 16;
	hashOut += (get_global_id(0) - get_global_offset(0)) * 16;
	
    local uint aesLUT0[256], aesLUT1[256], aesLUT2[256], aesLUT3[256];
	event_t ldsReady = async_work_group_copy(aesLUT0, aes_round_luts + 256 * 0, 256, 0);
	async_work_group_copy(aesLUT1, aes_round_luts + 256 * 1, 256, ldsReady);
	async_work_group_copy(aesLUT2, aes_round_luts + 256 * 2, 256, ldsReady);
	async_work_group_copy(aesLUT3, aes_round_luts + 256 * 3, 256, ldsReady);
	
	/* If you read the paper, there should be in theory 144 of those in SHAvite3-256 and
	a whopping 448 in SHAvite3-512 but apparently 16 are sufficient for SPHLIB and so is for me. */
	uint rko[16];
	for(uint init = 0; init < 4; init++) {
		for(uint col = 0; col < 4; col++) {
			rko[init * 4 + col] = input[init * 4 + col];
		}
	}
	
	/* At first glance, setting up rki seems to be deviating considerably from the SHAvite3-512 standard.
	The code is indeed not by Pornin (who is a go-to man for cryptography). In SPHLIB shavite3 implementation,
	Pornin initializes the rki values to subsequent message uints (shavite.c:964), but SHAvite3-512 uses a
	1024 bit message block while we have only 16*4*8=512bits in the hash we're going to hash (bleargh).
	So the question is: what are those values in legacy kernels? They are really the the message termination as
	specified at page 16 of Shavite.pdf, section 4.2.4, the main complications are
	- We are reasoning in LE format on uints so to set "the first byte in a stream" we have to set the lowest
	  byte in the uint
	- Some bounduaries, such as the padded message length modulo 1024 = 880 ends being at half of a uint so
	  in that case to write the length of the message we have to write the upper half, even though we're setting
	  the low half.
	- Similarly, /m/ in the last 16 bits is set to the same value.
	You can more easily visualize this using a spreadsheet.
	So, we take all those values, compute them once and have them there. 
	So basically, that's what the code in legacy kernels do (apparently by PHM, which is sph-sgminer author, 
	whose full nick is "prettyhatemachine". */
	uint rki[16];
	for(uint init = 0; init < 4; init++) {
		for(uint col = 0; col < 4; col++) {
			rki[init * 4 + col] = SHAvite3_512_precomputedPadding[init][col];
		}
	}
	
	uint hashing[4 * 4];
	for(uint init = 0; init < 4; init++) {
		for(uint col = 0; col < 4; col++) {
			hashing[init * 4 + col] = SHAvite3_512_IV[init][col];
		}
	}
	
	{ // LIB SPH, shavite.c:896, initialization moved out.
		uint p[16];  // it's called P in the paper.
		local uint x[4 * 64]; // temporary buffer
		for(uint cp = 0; cp < 16; cp++) p[cp] = hashing[cp];
		wait_group_events(1, &ldsReady);
		SHAvite3_Round( 0, p, rko, rki, x, aesLUT0, aesLUT1, aesLUT2, aesLUT3);
		SHAvite3_Round( 1, p, rko, rki, x, aesLUT0, aesLUT1, aesLUT2, aesLUT3);
		SHAvite3_Round( 2, p, rko, rki, x, aesLUT0, aesLUT1, aesLUT2, aesLUT3);
		SHAvite3_Round( 3, p, rko, rki, x, aesLUT0, aesLUT1, aesLUT2, aesLUT3);
		SHAvite3_Round( 4, p, rko, rki, x, aesLUT0, aesLUT1, aesLUT2, aesLUT3);
		SHAvite3_Round( 5, p, rko, rki, x, aesLUT0, aesLUT1, aesLUT2, aesLUT3);
		SHAvite3_Round( 6, p, rko, rki, x, aesLUT0, aesLUT1, aesLUT2, aesLUT3);
		SHAvite3_Round( 7, p, rko, rki, x, aesLUT0, aesLUT1, aesLUT2, aesLUT3);
		SHAvite3_Round( 8, p, rko, rki, x, aesLUT0, aesLUT1, aesLUT2, aesLUT3);
		SHAvite3_Round( 9, p, rko, rki, x, aesLUT0, aesLUT1, aesLUT2, aesLUT3);
		SHAvite3_Round(10, p, rko, rki, x, aesLUT0, aesLUT1, aesLUT2, aesLUT3);
		SHAvite3_Round(11, p, rko, rki, x, aesLUT0, aesLUT1, aesLUT2, aesLUT3);
		SHAvite3_Round(12, p, rko, rki, x, aesLUT0, aesLUT1, aesLUT2, aesLUT3);
		SHAvite3_Round(13, p, rko, rki, x, aesLUT0, aesLUT1, aesLUT2, aesLUT3);
		
		for(uint cp = 0; cp < 8; cp++) hashing[cp] ^= p[cp + 8];
		for(uint cp = 0; cp < 8; cp++) hashing[cp + 8] ^= p[cp];
	}
	for(uint cp = 0; cp < 16; cp++) hashOut[cp] = hashing[cp];
}
