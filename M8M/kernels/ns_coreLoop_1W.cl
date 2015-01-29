/*
 * Copyright (C) 2014 Massimo Del Zotto
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
/*
Neoscrypt.
After the FastKDF, there's a loop to be executed twice with different parameters.
In the first iteration, Salsa is used. In the second iteration ChaCha.

At a first glance, all those appear to be 4-way... see some notes on that.
*/


#if defined BLOCKMIX_SALSA
void SliceMixVEC(uint16 *tmp, uint mixRounds) {
	// In the beginning, this used a uint[16] and just took for granted the compiler would have
	// figured out it's all about constexpr.
	// It looks like **some** compilers are too lazy on figuring out something is constexpr so I had
	// to do this explicitly, pretty much like legacy kernels are doing.
	for(uint loop = 0; loop < mixRounds; loop++) {
		// First we mangle 4 independant columns. Each column starts on a diagonal cell so they are "rotated up" somehow.
		(*tmp).s4 ^= rotate((*tmp).s0 + (*tmp).sc, 7u);
		(*tmp).s8 ^= rotate((*tmp).s4 + (*tmp).s0, 9u);
		(*tmp).sc ^= rotate((*tmp).s8 + (*tmp).s4, 13u);
		(*tmp).s0 ^= rotate((*tmp).sc + (*tmp).s8, 18u);

		(*tmp).s9 ^= rotate((*tmp).s5 + (*tmp).s1, 7u);
		(*tmp).sd ^= rotate((*tmp).s9 + (*tmp).s5, 9u);
		(*tmp).s1 ^= rotate((*tmp).sd + (*tmp).s9, 13u);
		(*tmp).s5 ^= rotate((*tmp).s1 + (*tmp).sd, 18u);

		(*tmp).se ^= rotate((*tmp).sa + (*tmp).s6, 7u);
		(*tmp).s2 ^= rotate((*tmp).se + (*tmp).sa, 9u);
		(*tmp).s6 ^= rotate((*tmp).s2 + (*tmp).se, 13u);
		(*tmp).sa ^= rotate((*tmp).s6 + (*tmp).s2, 18u);

		(*tmp).s3 ^= rotate((*tmp).sf + (*tmp).sb, 7u);
		(*tmp).s7 ^= rotate((*tmp).s3 + (*tmp).sf, 9u);
		(*tmp).sb ^= rotate((*tmp).s7 + (*tmp).s3, 13u);
		(*tmp).sf ^= rotate((*tmp).sb + (*tmp).s7, 18u);

		// Then we mangle rows, again those are rotated. First is rotated 3, others are rotated less.
		// It would be easier to visualize that the other way around.
		(*tmp).s1 ^= rotate((*tmp).s0 + (*tmp).s3, 7u);
		(*tmp).s2 ^= rotate((*tmp).s1 + (*tmp).s0, 9u);
		(*tmp).s3 ^= rotate((*tmp).s2 + (*tmp).s1, 13u);
		(*tmp).s0 ^= rotate((*tmp).s3 + (*tmp).s2, 18u);

		(*tmp).s6 ^= rotate((*tmp).s5 + (*tmp).s4, 7u);
		(*tmp).s7 ^= rotate((*tmp).s6 + (*tmp).s5, 9u);
		(*tmp).s4 ^= rotate((*tmp).s7 + (*tmp).s6, 13u);
		(*tmp).s5 ^= rotate((*tmp).s4 + (*tmp).s7, 18u);

		(*tmp).sb ^= rotate((*tmp).sa + (*tmp).s9, 7u);
		(*tmp).s8 ^= rotate((*tmp).sb + (*tmp).sa, 9u);
		(*tmp).s9 ^= rotate((*tmp).s8 + (*tmp).sb, 13u);
		(*tmp).sa ^= rotate((*tmp).s9 + (*tmp).s8, 18u);

		(*tmp).sc ^= rotate((*tmp).sf + (*tmp).se, 7u);
		(*tmp).sd ^= rotate((*tmp).sc + (*tmp).sf, 9u);
		(*tmp).se ^= rotate((*tmp).sd + (*tmp).sc, 13u);
		(*tmp).sf ^= rotate((*tmp).se + (*tmp).sd, 18u);
	}
}
#endif

#if defined BLOCKMIX_CHACHA
void SliceMixVEC(uint16 *tmp, uint mixRounds) {
	for(uint loop = 0; loop < mixRounds; loop++) {
		// Here we have some mangling "by column".
		(*tmp).s0 += (*tmp).s4;    (*tmp).sc = rotate((*tmp).sc ^ (*tmp).s0, 16u);
		(*tmp).s8 += (*tmp).sc;    (*tmp).s4 = rotate((*tmp).s4 ^ (*tmp).s8, 12u);
		(*tmp).s0 += (*tmp).s4;    (*tmp).sc = rotate((*tmp).sc ^ (*tmp).s0, 8u);
		(*tmp).s8 += (*tmp).sc;    (*tmp).s4 = rotate((*tmp).s4 ^ (*tmp).s8, 7u);

		(*tmp).s1 += (*tmp).s5;    (*tmp).sd = rotate((*tmp).sd ^ (*tmp).s1, 16u);
		(*tmp).s9 += (*tmp).sd;    (*tmp).s5 = rotate((*tmp).s5 ^ (*tmp).s9, 12u);
		(*tmp).s1 += (*tmp).s5;    (*tmp).sd = rotate((*tmp).sd ^ (*tmp).s1, 8u);
		(*tmp).s9 += (*tmp).sd;    (*tmp).s5 = rotate((*tmp).s5 ^ (*tmp).s9, 7u);

		(*tmp).s2 += (*tmp).s6;    (*tmp).se = rotate((*tmp).se ^ (*tmp).s2, 16u);
		(*tmp).sa += (*tmp).se;    (*tmp).s6 = rotate((*tmp).s6 ^ (*tmp).sa, 12u);
		(*tmp).s2 += (*tmp).s6;    (*tmp).se = rotate((*tmp).se ^ (*tmp).s2, 8u);
		(*tmp).sa += (*tmp).se;    (*tmp).s6 = rotate((*tmp).s6 ^ (*tmp).sa, 7u);

		(*tmp).s3 += (*tmp).s7;    (*tmp).sf = rotate((*tmp).sf ^ (*tmp).s3, 16u);
		(*tmp).sb += (*tmp).sf;    (*tmp).s7 = rotate((*tmp).s7 ^ (*tmp).sb, 12u);
		(*tmp).s3 += (*tmp).s7;    (*tmp).sf = rotate((*tmp).sf ^ (*tmp).s3, 8u);
		(*tmp).sb += (*tmp).sf;    (*tmp).s7 = rotate((*tmp).s7 ^ (*tmp).sb, 7u);

		// Then we mix by diagonal.
		(*tmp).s0 += (*tmp).s5;    (*tmp).sf = rotate((*tmp).sf ^ (*tmp).s0, 16u);
		(*tmp).sa += (*tmp).sf;    (*tmp).s5 = rotate((*tmp).s5 ^ (*tmp).sa, 12u);
		(*tmp).s0 += (*tmp).s5;    (*tmp).sf = rotate((*tmp).sf ^ (*tmp).s0, 8u);
		(*tmp).sa += (*tmp).sf;    (*tmp).s5 = rotate((*tmp).s5 ^ (*tmp).sa, 7u);

		(*tmp).s1 += (*tmp).s6;    (*tmp).sc = rotate((*tmp).sc ^ (*tmp).s1, 16u);
		(*tmp).sb += (*tmp).sc;    (*tmp).s6 = rotate((*tmp).s6 ^ (*tmp).sb, 12u);
		(*tmp).s1 += (*tmp).s6;    (*tmp).sc = rotate((*tmp).sc ^ (*tmp).s1, 8u);
		(*tmp).sb += (*tmp).sc;    (*tmp).s6 = rotate((*tmp).s6 ^ (*tmp).sb, 7u);

		(*tmp).s2 += (*tmp).s7;    (*tmp).sd = rotate((*tmp).sd ^ (*tmp).s2, 16u);
		(*tmp).s8 += (*tmp).sd;    (*tmp).s7 = rotate((*tmp).s7 ^ (*tmp).s8, 12u);
		(*tmp).s2 += (*tmp).s7;    (*tmp).sd = rotate((*tmp).sd ^ (*tmp).s2, 8u);
		(*tmp).s8 += (*tmp).sd;    (*tmp).s7 = rotate((*tmp).s7 ^ (*tmp).s8, 7u);

		(*tmp).s3 += (*tmp).s4;    (*tmp).se = rotate((*tmp).se ^ (*tmp).s3, 16u);
		(*tmp).s9 += (*tmp).se;    (*tmp).s4 = rotate((*tmp).s4 ^ (*tmp).s9, 12u);
		(*tmp).s3 += (*tmp).s4;    (*tmp).se = rotate((*tmp).se ^ (*tmp).s3, 8u);
		(*tmp).s9 += (*tmp).se;    (*tmp).s4 = rotate((*tmp).s4 ^ (*tmp).s9, 7u);
	}
	/* I know what you're thinking. I'll do that 4-way!
	The 4-way core loop version has some interesting properties. It generates high amounts of cache hits and
	consumes just as much memory as it's supposed to BUT the extra instructions to make the LDS work slow down
	us a little (albeit not as much as one would expect). In theory we could use SIMD shuffle but it's not exposed
	in OpenCL yet.
	Interesting fact: 4-way can be formulated in LDS or "sliced" like there. For some reason, "sliced" version is
	faster than the LDS version (not exactly what we would expect from something that's supposed to be "memory hard")
	but still slower than 1-way sliced (due to the amount extra instruction to address values?).
	For a comparison, 4-way fully LDS takes 237/239 kOps SW/IR, vs 639/510 for the 1-way kernel.
	Albeit instruction count isn't a direct representation of performance in GCN, I think you can see where this is going. */
}
#endif


uint16 LoadStateSlice(global const uint *src) {
	uint16 value;
	value.s0 = src[ 0 * get_local_size(0)];
	value.s1 = src[ 1 * get_local_size(0)];
	value.s2 = src[ 2 * get_local_size(0)];
	value.s3 = src[ 3 * get_local_size(0)];
	value.s4 = src[ 4 * get_local_size(0)];
	value.s5 = src[ 5 * get_local_size(0)];
	value.s6 = src[ 6 * get_local_size(0)];
	value.s7 = src[ 7 * get_local_size(0)];
	value.s8 = src[ 8 * get_local_size(0)];
	value.s9 = src[ 9 * get_local_size(0)];
	value.sa = src[10 * get_local_size(0)];
	value.sb = src[11 * get_local_size(0)];
	value.sc = src[12 * get_local_size(0)];
	value.sd = src[13 * get_local_size(0)];
	value.se = src[14 * get_local_size(0)];
	value.sf = src[15 * get_local_size(0)];
	return value;
}


void StoreStateSlice(global uint *dst, const uint16 value) {
	dst[ 0 * get_local_size(0)] = value.s0;
	dst[ 1 * get_local_size(0)] = value.s1;
	dst[ 2 * get_local_size(0)] = value.s2;
	dst[ 3 * get_local_size(0)] = value.s3;
	dst[ 4 * get_local_size(0)] = value.s4;
	dst[ 5 * get_local_size(0)] = value.s5;
	dst[ 6 * get_local_size(0)] = value.s6;
	dst[ 7 * get_local_size(0)] = value.s7;
	dst[ 8 * get_local_size(0)] = value.s8;
	dst[ 9 * get_local_size(0)] = value.s9;
	dst[10 * get_local_size(0)] = value.sa;
	dst[11 * get_local_size(0)] = value.sb;
	dst[12 * get_local_size(0)] = value.sc;
	dst[13 * get_local_size(0)] = value.sd;
	dst[14 * get_local_size(0)] = value.se;
	dst[15 * get_local_size(0)] = value.sf;
}


uint16 LoadPadSlice(global const uint *src) {
	uint16 value;
	value.s0 = src[( 0 + get_local_id(0)) % 16];
	value.s1 = src[( 1 + get_local_id(0)) % 16];
	value.s2 = src[( 2 + get_local_id(0)) % 16];
	value.s3 = src[( 3 + get_local_id(0)) % 16];
	value.s4 = src[( 4 + get_local_id(0)) % 16];
	value.s5 = src[( 5 + get_local_id(0)) % 16];
	value.s6 = src[( 6 + get_local_id(0)) % 16];
	value.s7 = src[( 7 + get_local_id(0)) % 16];
	value.s8 = src[( 8 + get_local_id(0)) % 16];
	value.s9 = src[( 9 + get_local_id(0)) % 16];
	value.sa = src[(10 + get_local_id(0)) % 16];
	value.sb = src[(11 + get_local_id(0)) % 16];
	value.sc = src[(12 + get_local_id(0)) % 16];
	value.sd = src[(13 + get_local_id(0)) % 16];
	value.se = src[(14 + get_local_id(0)) % 16];
	value.sf = src[(15 + get_local_id(0)) % 16];
	return value;
}


void PreparePadBlock(local uint *dst, const uint16 value) {
	dst[( 0 + get_local_id(0)) % 16] = value.s0;
	dst[( 1 + get_local_id(0)) % 16] = value.s1;
	dst[( 2 + get_local_id(0)) % 16] = value.s2;
	dst[( 3 + get_local_id(0)) % 16] = value.s3;
	dst[( 4 + get_local_id(0)) % 16] = value.s4;
	dst[( 5 + get_local_id(0)) % 16] = value.s5;
	dst[( 6 + get_local_id(0)) % 16] = value.s6;
	dst[( 7 + get_local_id(0)) % 16] = value.s7;
	dst[( 8 + get_local_id(0)) % 16] = value.s8;
	dst[( 9 + get_local_id(0)) % 16] = value.s9;
	dst[(10 + get_local_id(0)) % 16] = value.sa;
	dst[(11 + get_local_id(0)) % 16] = value.sb;
	dst[(12 + get_local_id(0)) % 16] = value.sc;
	dst[(13 + get_local_id(0)) % 16] = value.sd;
	dst[(14 + get_local_id(0)) % 16] = value.se;
	dst[(15 + get_local_id(0)) % 16] = value.sf;
}


static constant uint slicePerm[2][4] = {
	{ 0, 1, 2, 3 },
	{ 0, 2, 1, 3 }
};


__attribute__((reqd_work_group_size(64, 1, 1)))
kernel void sequentialWrite_1way(global uint *xin, global uint *padBuffer,
 const uint iterations, // 128
 const uint xslices, // the value 4, so drivers won't unroll, on some drivers, save 8/54 registers!
 const uint mixRounds, // 10
 global uint *statex) {
	// Leave xin as is as it has to be reused next loop.
	// Copy to statex instead. It's super easy: they are the same thing.
	const uint slot = get_global_id(0) - get_global_offset(0);
	xin    += get_group_id(0) * get_local_size(0) * 64;
	statex += get_group_id(0) * get_local_size(0) * 64;
	for(uint cp = 0; cp < 64; cp++) statex[cp * get_local_size(0) + get_local_id(0)] = xin[cp * get_local_size(0) + get_local_id(0)];
	padBuffer += get_group_id(0) * get_local_size(0) * 16;
	local uint lds[16 * 64]; // one slice at time, for each hash +1 staggering uint each row
	local uint *mySlice = lds + get_local_id(0) * 16;
	// updated state from previous slice iteration, this starts with slice[3]
	xin    += get_local_id(0);
	statex += get_local_id(0);
	uint16 mangle = LoadStateSlice(xin + 16 * 3 * get_local_size(0));
	for(uint loop = 0; loop < iterations; loop++) {
		barrier(CLK_GLOBAL_MEM_FENCE);
		for(uint slice = 0; slice < 4; slice++) {
			barrier(CLK_LOCAL_MEM_FENCE);
			// Load up state to be used from state buffer and keep it around. In legacy kernels, this is left ^= right. Also goes to padbuffer.
			global uint *currentSlice = statex + slicePerm[loop % 2][slice] * 16 * get_local_size(0);
			const uint16 leftSlice = LoadStateSlice(currentSlice);
			PreparePadBlock(mySlice, leftSlice);
			event_t padOut = async_work_group_copy(padBuffer, lds, 16 * 64, 0);
			//StorePadSlice(padBuffer, leftSlice);
			padBuffer += 16 * get_global_size(0);
			// Input to slicemix is xor of those values. Keep them around as we need to add them later.
			mangle ^= leftSlice;
			const uint16 prev = mangle;
			SliceMixVEC(&mangle, 10);
			mangle += prev;
			StoreStateSlice(currentSlice, mangle);
			wait_group_events(1, &padOut);
		}
	}
}


__attribute__((reqd_work_group_size(64, 1, 1)))
kernel void indirectedRead_1way(global uint *xio, global const uint *padBuffer,
 const uint iterations, // 128
 const uint xslices,
 const uint mixRounds // 10
) {
	const uint slot = get_global_id(0) - get_global_offset(0);
	xio += get_group_id(0) * get_local_size(0) * 64;
	xio += get_local_id(0);
	padBuffer += 16 * slot;

	// updated state from previous slice iteration, this starts with slice[3]
	uint16 mangle = LoadStateSlice(xio + 16 * 3 * get_local_size(0));
	for(uint loop = 0; loop < iterations; loop++) {
		barrier(CLK_GLOBAL_MEM_FENCE);
		const uint indirected = xio[48 * get_local_size(0)] % 128;
		global const uint *padSlices = padBuffer + indirected * 64 * get_global_size(0);
		for(uint slice = 0; slice < 4; slice++) {
			// First of all, load state and xor it with something from the pad buffer.
			// In general, we need a single XOR per iteration, except for the first slice which need one extra slice
			// as it comes from a previous iteration.
			if(slice == 0) mangle ^= LoadPadSlice(padSlices + 3 * 16 * get_global_size(0));
			global uint *currSlice = xio + slicePerm[loop % 2][slice] * 16 * get_local_size(0);
			mangle ^= LoadStateSlice(currSlice);
			mangle ^= LoadPadSlice(padSlices + slice * 16 * get_global_size(0));
			const uint16 prev = mangle;
			SliceMixVEC(&mangle, 10);
			mangle += prev;
			StoreStateSlice(currSlice, mangle);
		}
	}
}
