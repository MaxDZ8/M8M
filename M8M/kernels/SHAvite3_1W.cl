/*
 * Copyright (C) 2014 Massimo Del Zotto
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
/*
The 14.9 driver messed up my implementation big way!
It unrolls nothing, resulting in previous version being only 6KiB of code.
Unfortunately, this also implies plenty of branching and most importantly...
Incredible amounts of scratch registers (likely due to registers to be addressed)!
If that's not enough, the new driver runs the previous kernels more than twice as fast!
So, I guess I have to rewrite SHAVite... it has always been fairly ugly.

This file is pretty low on comments. Previous version features more comments but
whatever it's easier to understand can be debated.
*/


/*! The basic building block of SHAVite-3 is the AES round.
Because of the way it's used this does not need a pointer to modify registers in-place
as the input parameter is always a temporary. */
uint4 AESR(uint4 val, uint4 k, local uint *lut0, local uint *lut1, local uint *lut2, local uint *lut3) {
#if __ENDIAN_LITTLE__
#define LUT(li, val)  (lut##li != 0? lut##li[(val >> (8 * li)) & 0xFF] : rotate(lut0[(val >> (8 * li)) & 0xFF], (8u * li##u)))
	uint4 result;
	result.s0 = lut0[val.s0 & 0xFF] ^ LUT(1, val.s1) ^ LUT(2, val.s2) ^ LUT(3, val.s3) ^ k.s0;
	result.s1 = lut0[val.s1 & 0xFF] ^ LUT(1, val.s2) ^ LUT(2, val.s3) ^ LUT(3, val.s0) ^ k.s1;
	result.s2 = lut0[val.s2 & 0xFF] ^ LUT(1, val.s3) ^ LUT(2, val.s0) ^ LUT(3, val.s1) ^ k.s2;
	result.s3 = lut0[val.s3 & 0xFF] ^ LUT(1, val.s0) ^ LUT(2, val.s1) ^ LUT(3, val.s2) ^ k.s3;
#undef LUT
#else
#error Endianness?
#endif
	return result;
}


uint4 AESRNK(uint4 val, local uint *lut0, local uint *lut1, local uint *lut2, local uint *lut3) {
	return AESR(val, (uint4)(0, 0, 0, 0), lut0, lut1, lut2, lut3);
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
static constant uint4 SHAvite3_512_precomputedPadding[4] = {
	(uint4)(0x00000080, 0x00000000, 0x00000000, 0x00000000), 
	(uint4)(0x00000000, 0x00000000, 0x00000000, 0x00000000), 
	(uint4)(0x00000000, 0x00000000, 0x00000000, 0x02000000), 
	(uint4)(0x00000000, 0x00000000, 0x00000000, 0x02000000)
};


/*! What is this thing? Used by some rounds to access rkI[n] registers
offset by one scalar. It's a fairly ugly thing as far as I'm concerned. Hopefully the compiler
will mangle everything in scalar (GCN is scalar). */
uint4 OffOne(uint4 *vals, uint start) {
	uint meh = start + 1;
	meh %= 8;
	meh = vals[meh].x;
	return (uint4)(vals[start].y, vals[start].z, vals[start].w, meh);
}


__attribute__((reqd_work_group_size(64, 1, 1)))
kernel void SHAvite3_1way(global uint *input, global uint *hashOut, global uint *aes_round_luts, const uint roundCount) {
#ifdef HEAD_OF_CHAINED_HASHING
// do nothing to input. We all fetch the same thing.
#else
	// get an hash from the previous stage.
	input   += (get_global_id(0) - get_global_offset(0)) * 16;
#endif
	hashOut += (get_global_id(0) - get_global_offset(0)) * 16;

	
	
    local uint TBL0[256], TBL1[256], TBL2[256], TBL3[256];
#define TABLES TBL0, TBL1, TBL2, TBL3
	event_t ldsReady = async_work_group_copy(TBL0, aes_round_luts + 256 * 0, 256, 0);
	async_work_group_copy(TBL1, aes_round_luts + 256 * 1, 256, ldsReady);
	async_work_group_copy(TBL2, aes_round_luts + 256 * 2, 256, ldsReady);
	async_work_group_copy(TBL3, aes_round_luts + 256 * 3, 256, ldsReady);
	
	uint4 rk[4+4], hashing[4], p[4];
	for(uint init = 0; init < 4; init++) {
		rk[0 + init].s0 = input[init * 4 + 0];
		rk[0 + init].s1 = input[init * 4 + 1];
		rk[0 + init].s2 = input[init * 4 + 2];
		rk[0 + init].s3 = input[init * 4 + 3];
		hashing[init].s0 = SHAvite3_512_IV[init][0];
		hashing[init].s1 = SHAvite3_512_IV[init][1];
		hashing[init].s2 = SHAvite3_512_IV[init][2];
		hashing[init].s3 = SHAvite3_512_IV[init][3];
		p[init] = hashing[init];
		#ifndef HEAD_OF_CHAINED_HASHING
			rk[4 + init] = SHAvite3_512_precomputedPadding[init];
		#endif
	}
	#ifdef HEAD_OF_CHAINED_HASHING
		// Some values are to be fetched differently. In theory I should do that right since the beginning
		// but it's easier to read this way.
		rk[4 + 0] = (uint4)(input[16], input[17], input[18], get_global_id(0));
		rk[4 + 1] = SHAvite3_512_precomputedPadding[0];
		rk[4 + 2] = (uint4)(0, 0, 0, 0x2800000);
		rk[4 + 3] = (uint4)(0, 0, 0, SHAvite3_512_precomputedPadding[3].w);	
		#if __ENDIAN_LITTLE__
			for(uint i = 0; i < 4; i++) {
				rk[0 + i].s0 = as_uint(as_uchar4(rk[0 + i].s0).wzyx);
				rk[0 + i].s1 = as_uint(as_uchar4(rk[0 + i].s1).wzyx);
				rk[0 + i].s2 = as_uint(as_uchar4(rk[0 + i].s2).wzyx);
				rk[0 + i].s3 = as_uint(as_uchar4(rk[0 + i].s3).wzyx);
			}
			rk[4 + 0].s0 = as_uint(as_uchar4(rk[4 + 0].s0).wzyx);
			rk[4 + 0].s1 = as_uint(as_uchar4(rk[4 + 0].s1).wzyx);
			rk[4 + 0].s2 = as_uint(as_uchar4(rk[4 + 0].s2).wzyx);
		#endif
	#endif
	wait_group_events(1, &ldsReady);	 
	{ // Round [0] is the easiest so let's have it there directly.
		uint4 temp; // in lib SPH, that's 'x'
		temp = AESRNK(p[1] ^ rk[0 + 0], TABLES);
		temp = AESRNK(temp ^ rk[0 + 1], TABLES);
		temp = AESRNK(temp ^ rk[0 + 2], TABLES);
		temp = AESRNK(temp ^ rk[0 + 3], TABLES);
		p[0] ^= temp;
		temp = AESRNK(p[3] ^ rk[4 + 0], TABLES);
		temp = AESRNK(temp ^ rk[4 + 1], TABLES);
		temp = AESRNK(temp ^ rk[4 + 2], TABLES);
		temp = AESRNK(temp ^ rk[4 + 3], TABLES);
		p[2] ^= temp;
	} // go to last round ([13]) for something slightly more complicated then go back there
	uint4 counter = 0; // 128bit counter, message length as standard.
	#ifdef HEAD_OF_CHAINED_HASHING
		counter.x = 20 * 4 * 8; // 640, 80<<3
	#else
		counter.x = 16 * 4 * 8; // 512, 64<<3
	#endif
	
	for(uint round = 1; round < roundCount - 1; ) { // notice those are somewhat a repeating block
		uint4 temp;
		// rounds [1][5][9] are quirky as they mix counter. Very much like [13]
		rk[0 + 0] = AESRNK(rk[0 + 0], TABLES).yzwx ^ rk[4 + 3];
		if(round == 1) rk[0 + 0] ^= (uint4)(counter.s0, counter.s1, counter.s2, ~counter.s3);
		                                                           temp = AESRNK(p[0] ^ rk[0 + 0], TABLES);
		rk[0 + 1] = AESRNK(rk[0 + 1], TABLES).yzwx ^ rk[0 + 0];
		if(round == 5) rk[0 + 1] ^= (uint4)(counter.s3, counter.s2, counter.s1, ~counter.s0);
		                                                           temp = AESRNK(temp ^ rk[0 + 1], TABLES);
		rk[0 + 2] = AESRNK(rk[0 + 2], TABLES).yzwx ^ rk[0 + 1];    temp = AESRNK(temp ^ rk[0 + 2], TABLES);
		rk[0 + 3] = AESRNK(rk[0 + 3], TABLES).yzwx ^ rk[0 + 2];    temp = AESRNK(temp ^ rk[0 + 3], TABLES);
		p[3] ^= temp;
		rk[4 + 0] = AESRNK(rk[4 + 0], TABLES).yzwx ^ rk[0 + 3];    temp = AESRNK(p[2] ^ rk[4 + 0], TABLES);
		rk[4 + 1] = AESRNK(rk[4 + 1], TABLES).yzwx ^ rk[4 + 0];    temp = AESRNK(temp ^ rk[4 + 1], TABLES);
		rk[4 + 2] = AESRNK(rk[4 + 2], TABLES).yzwx ^ rk[4 + 1];    temp = AESRNK(temp ^ rk[4 + 2], TABLES);
		rk[4 + 3] = AESRNK(rk[4 + 3], TABLES).yzwx ^ rk[4 + 2];
		if(round == 9) rk[4 + 3] ^= (uint4)(counter.s2, counter.s3, counter.s0, ~counter.s1);
		                                                           temp = AESRNK(temp ^ rk[4 + 3], TABLES);
		p[1] ^= temp;
		round++;
		// [2][6][10] are ALMOST simple but they access rk values differently!
		rk[0 + 0] ^= OffOne(rk, 4 + 2);    temp = AESRNK(p[3] ^ rk[0 + 0], TABLES);
		rk[0 + 1] ^= OffOne(rk, 4 + 3);    temp = AESRNK(temp ^ rk[0 + 1], TABLES);
		rk[0 + 2] ^= OffOne(rk, 0 + 0);    temp = AESRNK(temp ^ rk[0 + 2], TABLES);
		rk[0 + 3] ^= OffOne(rk, 0 + 1);    temp = AESRNK(temp ^ rk[0 + 3], TABLES);
		p[2] ^= temp;
		rk[4 + 0] ^= OffOne(rk, 0 + 2);    temp = AESRNK(p[1] ^ rk[4 + 0], TABLES);
		rk[4 + 1] ^= OffOne(rk, 0 + 3);    temp = AESRNK(temp ^ rk[4 + 1], TABLES);
		rk[4 + 2] ^= OffOne(rk, 4 + 0);    temp = AESRNK(temp ^ rk[4 + 2], TABLES);
		rk[4 + 3] ^= OffOne(rk, 4 + 1);    temp = AESRNK(temp ^ rk[4 + 3], TABLES);
		p[0] ^= temp;
		round++;
		// [3][7][11], the simplest in the loop. Rotating keys and update.
		// Basically the same as 1,5,9 but no counter mixing and different p
		rk[0 + 0] = AESRNK(rk[0 + 0], TABLES).yzwx ^ rk[4 + 3];    temp = AESRNK(p[2] ^ rk[0 + 0], TABLES);
		rk[0 + 1] = AESRNK(rk[0 + 1], TABLES).yzwx ^ rk[0 + 0];    temp = AESRNK(temp ^ rk[0 + 1], TABLES);
		rk[0 + 2] = AESRNK(rk[0 + 2], TABLES).yzwx ^ rk[0 + 1];    temp = AESRNK(temp ^ rk[0 + 2], TABLES);
		rk[0 + 3] = AESRNK(rk[0 + 3], TABLES).yzwx ^ rk[0 + 2];    temp = AESRNK(temp ^ rk[0 + 3], TABLES);
		p[1] ^= temp;
		rk[4 + 0] = AESRNK(rk[4 + 0], TABLES).yzwx ^ rk[0 + 3];    temp = AESRNK(p[0] ^ rk[4 + 0], TABLES);
		rk[4 + 1] = AESRNK(rk[4 + 1], TABLES).yzwx ^ rk[4 + 0];    temp = AESRNK(temp ^ rk[4 + 1], TABLES);
		rk[4 + 2] = AESRNK(rk[4 + 2], TABLES).yzwx ^ rk[4 + 1];    temp = AESRNK(temp ^ rk[4 + 2], TABLES);
		rk[4 + 3] = AESRNK(rk[4 + 3], TABLES).yzwx ^ rk[4 + 2];    temp = AESRNK(temp ^ rk[4 + 3], TABLES);
		p[3] ^= temp;
		round++;
		// [4][8][12], same as 2,6,10, only different p
		rk[0 + 0] ^= OffOne(rk, 4 + 2);    temp = AESRNK(p[1] ^ rk[0 + 0], TABLES);
		rk[0 + 1] ^= OffOne(rk, 4 + 3);    temp = AESRNK(temp ^ rk[0 + 1], TABLES);
		rk[0 + 2] ^= OffOne(rk, 0 + 0);    temp = AESRNK(temp ^ rk[0 + 2], TABLES);
		rk[0 + 3] ^= OffOne(rk, 0 + 1);    temp = AESRNK(temp ^ rk[0 + 3], TABLES);
		p[0] ^= temp;
		rk[4 + 0] ^= OffOne(rk, 0 + 2);    temp = AESRNK(p[3] ^ rk[4 + 0], TABLES);
		rk[4 + 1] ^= OffOne(rk, 0 + 3);    temp = AESRNK(temp ^ rk[4 + 1], TABLES);
		rk[4 + 2] ^= OffOne(rk, 4 + 0);    temp = AESRNK(temp ^ rk[4 + 2], TABLES);
		rk[4 + 3] ^= OffOne(rk, 4 + 1);    temp = AESRNK(temp ^ rk[4 + 3], TABLES);
		p[2] ^= temp;
		round++;
	}
	{ // Last round, usually [13] is very similar to [0] but...
		// 1- As all odd-numbered rounds, it has a "key expansion".
		// Lib SPH calls this "KEY_EXPAND_ELT". Simple AES round followed by a register rotate.
		// 2- It updates the freshly mangled values with some other values.
		// Note those "other values" are 4 uints "behind"
		// 3- Then it does as usual. Almost... mixing counter.
		// I do 1+2 in a single statement so I can column those nicely.
		uint4 temp;
		rk[0 + 0] = AESRNK(rk[0 + 0], TABLES).yzwx ^ rk[4 + 3];    temp = AESRNK(p[0] ^ rk[0 + 0], TABLES);
		rk[0 + 1] = AESRNK(rk[0 + 1], TABLES).yzwx ^ rk[0 + 0];    temp = AESRNK(temp ^ rk[0 + 1], TABLES);
		rk[0 + 2] = AESRNK(rk[0 + 2], TABLES).yzwx ^ rk[0 + 1];    temp = AESRNK(temp ^ rk[0 + 2], TABLES);
		rk[0 + 3] = AESRNK(rk[0 + 3], TABLES).yzwx ^ rk[0 + 2];    temp = AESRNK(temp ^ rk[0 + 3], TABLES);
		p[3] ^= temp;
		rk[4 + 0] = AESRNK(rk[4 + 0], TABLES).yzwx ^ rk[0 + 3];    temp = AESRNK(p[2] ^ rk[4 + 0], TABLES);
		rk[4 + 1] = AESRNK(rk[4 + 1], TABLES).yzwx ^ rk[4 + 0];    temp = AESRNK(temp ^ rk[4 + 1], TABLES);
		rk[4 + 2] = AESRNK(rk[4 + 2], TABLES).yzwx ^ rk[4 + 1] ^ (uint4)(counter.s1, counter.s0, counter.s3, ~counter.s2);
		                                                                           temp = AESRNK(temp ^ rk[4 + 2], TABLES);
		rk[4 + 3] = AESRNK(rk[4 + 3], TABLES).yzwx ^ rk[4 + 2];    temp = AESRNK(temp ^ rk[4 + 3], TABLES);
		p[1] ^= temp;
	}
	hashing[0] ^= p[2];
	hashing[1] ^= p[3];
	hashing[2] ^= p[0];
	hashing[3] ^= p[1];
	for(uint i = 0; i < 4; i++) {
		hashOut[i * 4 + 0] = hashing[i].x;
		hashOut[i * 4 + 1] = hashing[i].y;
		hashOut[i * 4 + 2] = hashing[i].z;
		hashOut[i * 4 + 3] = hashing[i].w;
	}
}
