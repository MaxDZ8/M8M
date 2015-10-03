
void Salsa20_simd_shuffle_1W(uint *dst, uint *src) {
	// Legacy implementation here do a real shit with a COMBINE macro taking
	// two uints to make an ulong so it can be... ? Everything is uint anyway!
	for(uint cp = 0; cp < 4; cp++) dst[ 0 + cp] = src[cp * 5];
	for(uint cp = 0; cp < 3; cp++) dst[ 4 + cp] = src[(cp + 1) * 5 - 1];
	for(uint cp = 0; cp < 3; cp++) dst[ 7 + cp] = src[(cp + 1) * 5 - 2];
	for(uint cp = 0; cp < 3; cp++) dst[10 + cp] = src[(cp + 1) * 5 - 3];
	for(uint cp = 0; cp < 3; cp++) dst[13 + cp] = src[(cp + 1) * 5 - 4];
}


void Salsa20_simd_unshuffle_1W(uint *dst, uint *src) {
	for(uint cp = 0; cp < 4; cp++) dst[cp * 4] = src[cp * 4];
	for(uint cp = 0; cp < 4; cp++) dst[cp * 4 + 1] = src[(cp * 4 + 1 + 12) % 16];
	for(uint cp = 0; cp < 4; cp++) dst[cp * 4 + 2] = src[(cp * 4 + 2 +  8) % 16];
	for(uint cp = 0; cp < 4; cp++) dst[cp * 4 + 3] = src[(cp * 4 + 3 +  4) % 16];
}


uint rotl(uint bits, uint amount) {
	return (bits << amount) | (bits >> (32 - amount));
	// return rotate(bits, 32 - amount);
}


void Salsa20_8_1W(uint *dancers) {
	uint x[16];
	Salsa20_simd_unshuffle_1W(x, dancers);
	for(uint i = 0; i < 8; i += 2) { // that's standard salsa implementation
		x[ 4] ^= rotl(x[ 0]+x[12], 7);  x[ 8] ^= rotl(x[ 4]+x[ 0], 9);
		x[12] ^= rotl(x[ 8]+x[ 4],13);  x[ 0] ^= rotl(x[12]+x[ 8],18);

		x[ 9] ^= rotl(x[ 5]+x[ 1], 7);  x[13] ^= rotl(x[ 9]+x[ 5], 9);
		x[ 1] ^= rotl(x[13]+x[ 9],13);  x[ 5] ^= rotl(x[ 1]+x[13],18);

		x[14] ^= rotl(x[10]+x[ 6], 7);  x[ 2] ^= rotl(x[14]+x[10], 9);
		x[ 6] ^= rotl(x[ 2]+x[14],13);  x[10] ^= rotl(x[ 6]+x[ 2],18);

		x[ 3] ^= rotl(x[15]+x[11], 7);  x[ 7] ^= rotl(x[ 3]+x[15], 9);
		x[11] ^= rotl(x[ 7]+x[ 3],13);  x[15] ^= rotl(x[11]+x[ 7],18);

		// those are said to be "on rows" but the rows are shifted so...
		x[ 1] ^= rotl(x[ 0]+x[ 3], 7);  x[ 2] ^= rotl(x[ 1]+x[ 0], 9);
		x[ 3] ^= rotl(x[ 2]+x[ 1],13);  x[ 0] ^= rotl(x[ 3]+x[ 2],18);

		x[ 6] ^= rotl(x[ 5]+x[ 4], 7);  x[ 7] ^= rotl(x[ 6]+x[ 5], 9);
		x[ 4] ^= rotl(x[ 7]+x[ 6],13);  x[ 5] ^= rotl(x[ 4]+x[ 7],18);

		x[11] ^= rotl(x[10]+x[ 9], 7);  x[ 8] ^= rotl(x[11]+x[10], 9);
		x[ 9] ^= rotl(x[ 8]+x[11],13);  x[10] ^= rotl(x[ 9]+x[ 8],18);

		x[12] ^= rotl(x[15]+x[14], 7);  x[13] ^= rotl(x[12]+x[15], 9);
		x[14] ^= rotl(x[13]+x[12],13);  x[15] ^= rotl(x[14]+x[13],18);
	}
	uint shuffled[16];
	Salsa20_simd_shuffle_1W(shuffled, x);
	for(uint i = 0; i < 16; i++) dancers[i] += shuffled[i];
}


void BlockMix_Salsa8_1W(uint *valout, uint *valin) {
	uint xval[16];
	for(uint cp = 0; cp < 16; cp++) xval[cp] = valin[16 + cp];
	for(uint cp = 0; cp < 16; cp++) xval[cp] ^= valin[cp];
	Salsa20_8_1W(xval);
	for(uint cp = 0; cp < 16; cp++) valout[cp] = xval[cp];
	for(uint cp = 0; cp < 16; cp++) xval[cp] ^= valin[cp + 16];
	Salsa20_8_1W(xval);
	for(uint cp = 0; cp < 16; cp++) valout[cp + 16] = xval[cp];
}


// Should really be called Longify for me but I'm preferring the legacy name.
// The comments in legacy implementation write about the SIMD order and host order,
// but I don't work in SIMD mode so I take it differently.
ulong Integerify(uint *block, const uint r) {
	uint *x = block + (2 * r - 1) * 16;
	uint hi = x[0];
	uint lo = x[13];
	return ((ulong)lo << 32) | hi;
}

#define YESCRYPT_RW 1


static constant const uchar simd_shuffle[2 * 8] = {
	0, 13, 10,  7,  4, 1, 14, 11,
	8,  5,  2, 15, 12, 9, 6 ,  3
};


// The first smix1 call in yescrypt as used by GBST-Y is called as:
// smix1(ulong *B, r=1, N=64, flags, ulong *V, NROM=0, ulong *XY, ulong *S=NULL);
//                                      ^ previously "Sp"
// The goal is a bit difficult to follow because of some pointer renaming in legacy implementation
// but it is indeed to begin filling *v (which is from *Sp, which is from *S).
// It is a "not so temporary" scratchpad, with the given parameters, it takes 8 KiB each hash.
kernel void firstSmix1(global uint *buffV, global uint *valB, const uint yescrypt_N) {
	const uint yescrypt_r = 1;
    //const uint yescrypt_N = 64;
    const uint yescrypt_s = 16 * 2; // *2 because we're on uints, yescrypt-opt in ulongs

	const uint slot = get_global_id(0) - get_global_offset(0);
	valB += slot * 256; // stride 1 kib --> almost guaranteed pain (but I can easily solve this).
	buffV += slot * 2048; // stride n*2048 --> guaranteed pain! But it's ok for the time being, this is fast anyway.

	uint valX[32];
	for(uint loop = 0; loop < 2 * yescrypt_r; loop++) {
		global uint *src = valB + loop * 16;
		uint *dst = valX + loop * 16;
		for(uint cp = 0; cp < 16; cp++) dst[simd_shuffle[cp]] = as_uint(as_uchar4(src[cp]).wzyx);
		// This comes from the previous kernel and it's already shuffled and in correct byte order to become ulong
		for(uint cp = 0; cp < 16; cp++) buffV[cp + loop * 16] = dst[cp];
	}
	// Now, the generic yescrypt implementation is a bit more complicated in structure
	// while I put everything here... with r=1 it ends up being just this (xor is commutative)
	for(uint loop = 0; loop < 16; loop++) valX[loop] ^= valX[loop + 16];
	Salsa20_8_1W(valX + 0);
	for(uint loop = 0; loop < 16; loop++) buffV[loop + 2 * 16] = valX[loop];
	for(uint loop = 0; loop < 16; loop++) valX[loop] ^= valX[loop + 16];
	Salsa20_8_1W(valX + 0);
	for(uint loop = 0; loop < 16; loop++) buffV[loop + 3 * 16] = valX[loop];
	
	// Inside an 'else', original comment is
	/* 4: X <-- H(X) */
	// This is basically the same thing but with the values we just produced.
	// The first thing salsa does is to xor the two blocks so, similarly to above...
	barrier(CLK_GLOBAL_MEM_FENCE); // we wrote stuff here previously, make sure we can re-read.
	for(uint loop = 0; loop < 16; loop++) valX[loop] ^= buffV[loop + 16 * 2];
	Salsa20_8_1W(valX);
	for(uint loop = 0; loop < 16; loop++) valX[loop + 16] = valX[loop] ^ buffV[loop + 16 * 3];
	Salsa20_8_1W(valX + 16);

	// Now I implement a loop found at yescrypt-opt:453. Original comment reads
	/* 2: for i = 0 to N - 1 do */
	// Solar Designer must be truly trolling us here.
	for(uint n = 1, loop = 2; loop < yescrypt_N; loop += 2) {
    	for(uint cp = 0; cp < yescrypt_s; cp++) buffV[loop * yescrypt_s + cp] = valX[cp];
    	if(YESCRYPT_RW) {
    		uint prev = loop - 1;
    		n = n << (loop & prev? 0 : 1); // this is only done on even loop value
    		ulong j = Integerify(valX, yescrypt_r) & (n - 1);
    		j += loop - n;

    		barrier(CLK_GLOBAL_MEM_FENCE);
    		for(uint cp = 0; cp < yescrypt_s; cp++) valX[cp] ^= buffV[j * yescrypt_s + cp];
    	}
		// Another
		/* 4: X <-- H(X) */
		// but this time we're in a loop.
    	uint yWork[32];
    	BlockMix_Salsa8_1W(yWork, valX);
    	for(uint cp = 0; cp < yescrypt_s; cp++) buffV[(loop + 1) * yescrypt_s + cp] = yWork[cp];
    	if(YESCRYPT_RW) {
    		ulong j = Integerify(yWork, yescrypt_r) & (n - 1);
    		j += (loop + 1) - n;
    		barrier(CLK_GLOBAL_MEM_FENCE);
    		for(uint cp = 0; cp < yescrypt_s; cp++) yWork[cp] ^= buffV[j * yescrypt_s + cp];
    	}
		// Yet another
		/* 4: X <-- H(X) */
		// but this time we're at the end of the loop. In yescrypt-opt.c it's the last statement of the loop as well.
    	BlockMix_Salsa8_1W(valX, yWork);
    }

	// To finish first call to smix1 I must update the B values with my current yWork.
	for(uint loop = 0; loop < 2; loop++) {
		uint *src = valX + loop * 16;
		uint temp[16];
		Salsa20_simd_unshuffle_1W(temp, src);
		for(uint cp = 0; cp < 16; cp++) valB[loop * 16 + cp] = as_uint(as_uchar4(temp[cp]).wzyx);
	}
}
