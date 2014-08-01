/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
/* SIMD is a rather complicated hash function.
The documentation is fairly extensive, in both theorical and practical terms, with several
implementations provided for reference and speed evaluations.
It is basically made up of two steps. The first is almost embarassingly parallel,
the second is 8-way... what I'm trying to do here is to build up a performance advantage
in the first half so even if the second half gets divergent threads, I still get a gain.

It turns out the amount of divergence is greater than expected so the gain is very diluted,
there are two ways to fix that:
- Move the divergent path out: this would make it embarassingly parallel. I've currently
  written that here as it was written in lib SPH and legacy miners, but it would be much
  more efficient to have it in a proper step, as noted in SIMD documentation.
- Try doing that 8-way (as suggested by documentation). This would solve the divergence
  problem perfectly but I'm going to lose at least one tick in occupancy. */

#define SIMD_REDUCE_BYTE_LUT 0


// A foundamental piece of SIMD architecture used mainly in SIMD16W_MangleInput and in a loop
// beginning what I call the "post-processing" stage.
// In SIMD documentation provided by the Ecole Normale this is a macro REDUCE(x).
// Documentation reads: Reduce modulo 257; result is in [-127; 383]
int SIMD_ByteReduce(int value) {
	return (value & 0x000000FF) - (value >>  8);
}

// Used in reduction loops, called with argument multiplied by alpha.
// See ReductionLoop for more info.
// In SIMD documentation provided by the Ecole Normale this is a macro REDUCE_EXTRA_S(x).
// Documentation reads: Reduce from [-127; 383] to [-128; 128]
int SIMD_ShortReduce(int value) {
	return (value & 0x0000FFFF) + (value >> 16);
}


// This is called "FFT8" in legacy kernels. The implementation is very similar to legacy.
// I just fetch directly from memory (instead of regs) and write to LDS (instead of other regs).
int8 SIMD16W_MangleInput(global uchar *input, uint offset) {
	int x[4];
	for(uint loop = 0; loop < 4; loop++) {
		x[loop] = offset < 64? input[offset] : 0; // select is inlined for scalars
		offset += 32;
	}	
	int a[4];
	for(uint loop = 0; loop < 2; loop++) {
		a[loop * 2 + 0] = x[0] + x[2];
		a[loop * 2 + 1] = x[0] + x[2] << 4;
	}
	int b[4];
	b[0] = x[1] + x[3];
	b[2] = (x[1] << 4) - (x[3] << 4);
#if !defined(SIMD_REDUCE_BYTE_LUT) || SIMD_REDUCE_BYTE_LUT == 0
	b[1] = SIMD_ByteReduce((x[1] << 2) + (x[3] << 6));
	b[3] = SIMD_ByteReduce((x[1] << 6) + (x[3] << 2));
	/* Values here have special properties. Guaranteed to be [0..255] and
	very likely being (0, u) or even (0, 0). Therefore very high coherence with
	late values having the same value. I cannot risk bank collisions here, use L1 instead. */
#else
#if SIMD_REDUCE_BYTE_LUT > 1
	b[1] = reduced[x[1] * 256 + x[3]];	
#else
	b[1] = SIMD_ByteReduce((x[1] << 2) + (x[3] << 6));
#endif
	b[3] = reduced[x[3] * 256 + x[1]];	
#endif
	return (int8)(a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3],
	              a[0] - b[0], a[1] - b[1], a[2] - b[2], a[3] - b[3]);
}


// Define LDS layout. Never delete this, it is nice at least for documentation purposes.
// Even though we use this, it is called relatively sparingly and then we work on relative movements.
// Therefore stuff will break very likely if this is changed.
uint LDSIDX(uint column, uint slot, uint hashIndex) {
	uint general = column + (hashIndex % 2? 16 : 0); // odd hashes right half of LDS banks
	general += slot * 32; // LDS line length in 4byte words, state is stored "rotated"
	return general + (hashIndex / 2? 32 * 16 : 0);
}

// linear index in canonical w state --> LDS offset in real work array
uint LDSLINEAR(uint i) {
	uint col = i / 16;
	uint slot = i % 16;
	return LDSIDX(col, slot, get_local_id(1));
}


/* Reformulation of FFT16 in legacy kernels. I can take it easy as I have plenty of regs now!
It is unfortunate I cannot just write a loop so I write a function instead. */
void SIMD16W_PrimeLDS(local int *col, const int8 one, const int8 two) {
	// In line of theory I would have to use LDSIDX everywhere but in practice I don't.
	col[ 0 * 32] = one.s0 + (two.s0 << 0);
	col[ 1 * 32] = one.s1 + (two.s1 << 1);
	col[ 2 * 32] = one.s2 + (two.s2 << 2);
	col[ 3 * 32] = one.s3 + (two.s3 << 3);
	col[ 4 * 32] = one.s4 + (two.s4 << 4);
	col[ 5 * 32] = one.s5 + (two.s5 << 5);
	col[ 6 * 32] = one.s6 + (two.s6 << 6);
	col[ 7 * 32] = one.s7 + (two.s7 << 7);
	col[ 8 * 32] = one.s0 - (two.s0 << 0);
	col[ 9 * 32] = one.s1 - (two.s1 << 1);
	col[10 * 32] = one.s2 - (two.s2 << 2);
	col[11 * 32] = one.s3 - (two.s3 << 3);
	col[12 * 32] = one.s4 - (two.s4 << 4);
	col[13 * 32] = one.s5 - (two.s5 << 5);
	col[14 * 32] = one.s6 - (two.s6 << 6);
	col[15 * 32] = one.s7 - (two.s7 << 7);
}


/*! Completely mangle state, all 256 values in LDS... somehow.
Legacy kernels have unrolled macros to do that as they use registers which cannot be
dynamically accessed (and they don't trust the compiler to guess).
LDS can be accessed dynamically, albeit at some cost and with some care so I just use a function.
Note this does not work as you might expect.
It is fairly simple to understand when called with parameter 0, operation which legacy kernels call
FFT_LOOP_16_8. In that case, WI0 will have to access the values "owned" by WI1 and will mangle both.
With exponent 1, WI0 will access and mangle WI2, WI1 will require WI3. So basically we divide the
state in intervals and somehow a "merge" merge them "reducing" two N*16-value intervals to
a single 2*N*16-value interval. The process iterates on all 256 state values and continues until we
get a 256-value interval. 
/param exponentOffset valid values are 0..3 included. 
 - 0 produces LOOP_16_8 (16*8=128, two values set per loop)
 - 1 produces LOOP_32_4
 - 2 produces LOOP_64_2
 - 3 produces LOOP_128_1 */
void SIMD16W_MergeIntervals(uint exponent, local int *state, global short *alpha) {
	exponent = min(exponent, 3u);
	const uint intervals = 8 >> exponent; // this is a constant
	const uint ilen = 16 / intervals; // this is workgroup x size, but if that changes odds are things will have to be redesigned anyway
	const uint step = ((16/16) << exponent); // stored 16 values by column
	/* Now we have some additional details sorted out, some facts:
	- There are always at least 16 iterations to execute and therefore always at least 16+16 values modified.
	- Because alpha[0] is 1 --> the value to offset existing values is the 'n' value. Legacy kernels squeeze out
	  an extra bit of performance by avoiding calling ShortReduce.
	  - This is valid because values are pulled out of ubyte in SIMD16W_MangleInput: it spits out at most 9 bits.
	    They're always either added or subtracted so they are guaranteed to be fitting a short.
	  - It is a bit more surprising this property holds even in other passes such as LOOP_32_4. This happens
	    because ShortReduce, no matter what, throws away the upper 16 bits, perhaps producing negative numbers, which
		are not very much more complicated. 
	  - I cannot do that as I want everything to be coherent. 
	What I do is fairly different. Starting point: we have N=16 WIs. Each WI gets to work on 2 values (n,m).
	So here's what we do. We subdivide the local-x WIs in groups of two. Both will work on column x, x+step
	but while the first thread pulls the first value from column x, the second pulls from x+step so the two
	loads can be performed in parallel. The two threads work on row a, a+1.
	We then "flow down" the columns (which are rows, since LDS is transposed) until we mangled all 16 elements in those two cols.
	There are other (N/2)-1 groups of two WIs. They are dispatched to following columns. */
	const uint partition = get_local_id(0) / ilen;
	const uint group = (get_local_id(0) - partition * ilen) / 2;
	const bool odd = get_local_id(0) % 2 != 0; // odd WI mangles odd lines
	local int *one = state + partition * ilen + group + (odd? step : 0);
	local int *two = state + partition * ilen + group + (odd? 0 : step);
	one += odd? 32 : 0;
	two += odd? 32 : 0;
	uint ax = get_local_id(0) % 2? 1 : 0;
	ax *= intervals;
	ax += group * intervals * 2 * 8; // the other group is like starting from some other iteration
	for(uint row = get_local_id(0) % 2; row < 16; row += 2) {
		const int maybeM = *one;
		const int maybeN = *two;
		const int n = odd? maybeM : maybeN;
		const int m = odd? maybeN : maybeM;
		const int t = SIMD_ShortReduce(n * alpha[ax]);
		*one = m + t * (odd? -1 : 1);
		*two = m - t * (odd? -1 : 1);	
		one += 64;
		two += 64;
		ax += intervals * 2;
	}
} // we're done! Easier using real GPU computing, isn't it?


uint ABCDOFF(uint vec, uint el) {
	uint off = get_local_id(1) * 16;
	off += (vec / 2) * 64;
	off += (vec % 2) * 8;
	return off + el + 1;
}


// SIMD.pdf, pg 19. Rotation constants to be used in the 4 rounds.
// The various elements of a vector are caller PI0, PI1, PI2, PI3 in the paper.
// Magic numbers happen very often in cryptography. Don't ask. That's it, period.
static constant uint4 SIMD_ROROT[4] = { // "round rotations"
	(uint4)( 3, 23, 17, 27),
	(uint4)(28, 19, 22,  7),
	(uint4)(29,  9, 15,  5),
	(uint4)( 4, 13, 10, 25)	
};


// Magic numbers for SIMD-512, pg 23.
static constant uint SIMD_IV_512[4][8] = {
	{ 0x0BA16B95, 0x72F999AD, 0x9FECC2AE, 0xBA3264FC, 0x5E894929, 0x8E9F30E5, 0x2F1DAA37, 0xF0F2C558 },
	{ 0xAC506643, 0xA90635A5, 0xE25B878B, 0xAAB7878F, 0x88817F7A, 0x0A02892B, 0x559A7550, 0x598F657E },
	{ 0x7EEF60A1, 0x6B70E3E8, 0x9C1714D1, 0xB958E2A8, 0xAB02675E, 0xED1C014F, 0xCD8D65BB, 0xFDB7A257 },
	{ 0x09254899, 0xD699C7BC, 0x9019B6DC, 0x2B9022E4, 0x8FA14956, 0x21BF9BD3, 0xB94D0943, 0x6FFDDC22 }
};


// Step permutations for A(n+1) values to be cross-fetched. SIMD.pdf, page 18
// Legacy kernels: see PP8_n_m macros
static constant uchar SIMD_ROUT_PERM_512[7][8] = {
	{ 1, 0, 3, 2, 5, 4, 7, 6 }, // j = 0 XOR 1
	{ 6, 7, 4, 5, 2, 3, 0, 1 }, // j = 1 XOR 6
	{ 2, 3, 0, 1, 6, 7, 4, 5 }, // j = 2 XOR 2
	{ 3, 2, 1, 0, 7, 6, 5, 4 }, // j = 3 XOR 3
	{ 5, 4, 7, 6, 1, 0, 3, 2 }, // j = 4 XOR 5
	{ 7, 6, 5, 4, 3, 2, 1, 0 }, // j = 5 XOR 7 
	{ 4, 5, 6, 7, 0, 1, 2, 3 }  // j = 6 XOR 4
};


// Rounds don't consider the expanded message, but rather a 32x8 matrix
// where each row is 8 elements long. The rows of this matrix however are to be
// accessed in a different order.
// Basically (page 15) Wj^(i) = Zj^(P(i))
// reference.c:103.
// SPH lib has an interesting way of doing that similar in concept to round permutation:
// a set of defined WB_n_m, see simd.h:1461
// This involves broadcasts and would be awesome to have in LDS.
static constant uchar SIMD_WZ_ROW_PERM[4][8] = {
	{  4,  6,  0,  2,  7,  5,  3,  1 },
	{ 15, 11, 12,  8,  9, 13, 10, 14 },
	{ 17, 18, 23, 20, 22, 21, 16, 19 },
	{ 30, 24, 25, 31, 27, 29, 28, 26 }
};


// page 10
uint SIMD_IF (uint ai, uint bi, uint ci) { return (ai & bi) | (~ai & ci); }
uint SIMD_MAJ(uint ai, uint bi, uint ci) { return (ci & bi) | ((ci | bi) & ai);}


#define STEP_FUNC_IF  1
#define STEP_FUNC_MAJ 2


/*! Doing 16-way parallel steps is hard! In retrospect, using 8-way might have been a better
idea since the previous steps are fast anyway. I use a fairly different approach, in which I
evolve the ABCD state "incrementally", in the hope to minimize divergence.
There are 32 values in the ABCD matrix so it can be spread among 16 threads. There is going to be
both some wasting (oldschool-gpu-style) and divergence (new-gen-gpu-style).
The bottom line is we keep old state in WI private registers, but we slap new values to LDS
even before having them final. 
Legacy kernels use a fairly convoluted set of macros to directly access registers there and
thus have a total advantage. I am therefore wasting instruction, divergence and require more
registers in the first place. Hopefully this won't eat all my 200% performance over legacy so far!
\param[in] funcID must be either STEP_FUNC_IF or STEP_FUNC_MAJ, make sure this is set to
	compile time constant so it can be inlined or performance will suffer. */
void SIMD16W_Step(local int *abcd, uint roundIndex, uint stepIndex, const int mixin, uint r, uint s, uint funcID) {
	// First thing to do is the easier: columns BC get moved to the right 1 column.
	const uint channel = get_local_id(0) % 8;
	const uint colBC = get_local_id(0) < 8? 1 : 2;
	const int prevBCi = abcd[ABCDOFF(colBC, channel)];
	
	// 2nd: new columns AB is difficult, but B in particular comes handy to have there so
	// we can fetch it nicely from the permutation.
	const uint srcAD = get_local_id(0) < 8? 0 : 3;
	const uint dstAD = get_local_id(0) < 8? 1 : 0;
	int prevADi = abcd[ABCDOFF(srcAD, channel)];
	const int amount = srcAD == 0? r : s;
	
	if(dstAD == 0) { // I'm sorry for that. I cannot quite work it out.
		const int a = abcd[ABCDOFF(0, channel)];
		const int b = abcd[ABCDOFF(1, channel)];
		const int c = prevBCi;
		switch(funcID) { // make sure it's compile-time constant!
			case STEP_FUNC_IF : prevADi += SIMD_IF (a, b, c); break;
			case STEP_FUNC_MAJ: prevADi += SIMD_MAJ(a, b, c); break;
		}
		prevADi += mixin;
	}
	
	// Now we can start writing to matrix.
	abcd[ABCDOFF(dstAD, channel)] = rotate(prevADi, amount);
	abcd[ABCDOFF(colBC + 1, channel)] = prevBCi;
	barrier(CLK_LOCAL_MEM_FENCE);
	if(dstAD == 0) {
		// In line of principle I should be doing (roundIndex * 8 + stepIndex) here
		// but since 8 % 7 is 1 this means I can just add them.
		int perm = SIMD_ROUT_PERM_512[(roundIndex + stepIndex) % 7][channel];
		abcd[ABCDOFF(0, channel)] += abcd[ABCDOFF(1, perm)];
	}
	barrier(CLK_LOCAL_MEM_FENCE);
}


int SIMD_Inner(uint roundI, uint stepI, const local int *work) {
	// I also need to pull out the w value.
	// It is a very ugly thing in legacy kernels. Not much better here.
	const int rindex = SIMD_WZ_ROW_PERM[roundI][stepI];
	const int offo = roundI < 2? 0   : (roundI == 2? -256 : -383);
	const int offi = roundI < 2? 1   : (roundI == 2? -128 : -255);
	const uint mul = roundI < 2? 185 : 233;
	work += LDSIDX(0, 0, get_local_id(1));
	uint2 linear = (uint2)(16 * rindex + 2 * (get_local_id(0) % 8));
	linear += (uint2)(offo, offi);
	linear = (linear / 16) * 32 + (linear % 16);
	// expand from F_257 to 2^32
	uint lo = (uint)(work[linear.x] * mul);
	uint hi = (uint)(work[linear.y] * mul);
	return (lo & 0x0000FFFFu) + (hi << 16);
}

/*
P.19, a block of those eight steps is called a round. The order of pi is defined in the spec,
as well as two functions to be used. Also see reference.c:88.
My round function looks quite different because steps mix values taken from the work array BUT
I will need to call those providing constants later and ue to a OpenCL1.x limitation the pointer
must either be local or constant. The soultion is to pull it out and compute it there.
On the cons: make sure the conditional is the same as specified in the step. */
void SIMD16W_Round(local int *abcd, local const int *work, const uint4 pi, uint round) {
	int8 mixin = 0;
	if(get_local_id(0) >= 8) {
	    mixin.s0 = SIMD_Inner(round, 0, work);
	    mixin.s1 = SIMD_Inner(round, 1, work);
	    mixin.s2 = SIMD_Inner(round, 2, work);
	    mixin.s3 = SIMD_Inner(round, 3, work);
	    mixin.s4 = SIMD_Inner(round, 4, work);
	    mixin.s5 = SIMD_Inner(round, 5, work);
	    mixin.s6 = SIMD_Inner(round, 6, work);
	    mixin.s7 = SIMD_Inner(round, 7, work);
	}
	SIMD16W_Step(abcd, round, 0, mixin.s0, pi.s0, pi.s1, STEP_FUNC_IF);
	SIMD16W_Step(abcd, round, 1, mixin.s1, pi.s1, pi.s2, STEP_FUNC_IF);
	SIMD16W_Step(abcd, round, 2, mixin.s2, pi.s2, pi.s3, STEP_FUNC_IF);
	SIMD16W_Step(abcd, round, 3, mixin.s3, pi.s3, pi.s0, STEP_FUNC_IF);
	SIMD16W_Step(abcd, round, 4, mixin.s4, pi.s0, pi.s1, STEP_FUNC_MAJ);
	SIMD16W_Step(abcd, round, 5, mixin.s5, pi.s1, pi.s2, STEP_FUNC_MAJ);
	SIMD16W_Step(abcd, round, 6, mixin.s6, pi.s2, pi.s3, STEP_FUNC_MAJ);
	SIMD16W_Step(abcd, round, 7, mixin.s7, pi.s3, pi.s0, STEP_FUNC_MAJ);
}

void WTranspose(local int *work) {
	const uint dim = 16;
	const uint overlapping = dim % 2? 0 : 1;
	const uint col = get_local_id(0);
	const uint hash = get_local_id(1);
	for(uint loop = 0; loop < dim / 2 - overlapping; loop++) {
		const uint row = (get_local_id(0) + loop + 1) % dim;
		const int a = work[LDSIDX(col, row, hash)];
		work[LDSIDX(col, row, hash)] = work[LDSIDX(row, col, hash)];
		work[LDSIDX(row, col, hash)] = a;
	}
	if(overlapping && get_local_id(0) < dim / 2) {
		const uint row = get_local_id(0) + dim / 2;
		const int a = work[LDSIDX(col, row, hash)];
		work[LDSIDX(col, row, hash)] = work[LDSIDX(row, col, hash)];
		work[LDSIDX(row, col, hash)] = a;
	}
	barrier(CLK_LOCAL_MEM_FENCE);
}


constant short SIMD512_MESSAGE1024BIT_LAST_BLOCK_W[16][16] = {  
	{ 0x0004, 0x001c, 0xffb0, 0xff88, 0xffd1, 0xff82, 0x002d, 0xff85,    0xffa4, 0xff81, 0xffba, 0x0017, 0xffe9, 0xffe8, 0x0028, 0xff83 },
	{ 0x0065, 0x007a, 0x0022, 0xffe8, 0xff89, 0x006e, 0xff87, 0xff90,    0x0020, 0x0018, 0x0033, 0x0049, 0xff8b, 0xffc0, 0xffeb, 0x002a },
	{ 0xffc4, 0x0010, 0x0005, 0x0055, 0x006b, 0x0034, 0xffd4, 0xffa0,    0x002a, 0x007f, 0xffee, 0xff94, 0xffd1, 0x001a, 0x005b, 0x0075 },
	{ 0x0070, 0x002e, 0x0057, 0x004f, 0x007e, 0xff88, 0x0041, 0xffe8,    0x0079, 0x001d, 0x0076, 0xfff9, 0xffcb, 0x0055, 0xff9e, 0xff8b },
	{ 0x0020, 0x0073, 0xffd1, 0xff8c, 0x003f, 0x0010, 0xff94, 0x0031,    0xff89, 0x0039, 0xff92, 0x0004, 0xffb4, 0xffb4, 0xffd6, 0xffaa },
	{ 0x003a, 0x0073, 0x0004, 0x0004, 0xffad, 0xffcd, 0xffdb, 0x0074,    0x0020, 0x000f, 0x0024, 0xffd6, 0x0049, 0xff9d, 0x005e, 0x0057 },
	{ 0x003c, 0xffec, 0x0043, 0x000c, 0xffb4, 0x0037, 0x0075, 0xffbc,    0xffae, 0xffb0, 0x005d, 0xffec, 0x005c, 0xffeb, 0xff80, 0xffa5 },
	{ 0xfff5, 0x0054, 0xffe4, 0x004c, 0x005e, 0xff84, 0x0025, 0x005d,    0x0011, 0xffb2, 0xff96, 0xffe3, 0x0058, 0xfff1, 0xffd1, 0x0066 },
	{ 0xfffc, 0xffe4, 0x0050, 0x0078, 0x002f, 0x007e, 0xffd3, 0x007b,    0x005c, 0x007f, 0x0046, 0xffe9, 0x0017, 0x0018, 0xffd8, 0x007d },
	{ 0xff9b, 0xff86, 0xffde, 0x0018, 0x0077, 0xff92, 0x0079, 0x0070,    0xffe0, 0xffe8, 0xffcd, 0xffb7, 0x0075, 0x0040, 0x0015, 0xffd6 },
	{ 0x003c, 0xfff0, 0xfffb, 0xffab, 0xff95, 0xffcc, 0x002c, 0x0060,    0xffd6, 0xff81, 0x0012, 0x006c, 0x002f, 0xffe6, 0xffa5, 0xff8b },
	{ 0xff90, 0xffd2, 0xffa9, 0xffb1, 0xff82, 0x0078, 0xffbf, 0x0018,    0xff87, 0xffe3, 0xff8a, 0x0007, 0x0035, 0xffab, 0x0062, 0x0075 },
	{ 0xffe0, 0xff8d, 0x002f, 0x0074, 0xffc1, 0xfff0, 0x006c, 0xffcf,    0x0077, 0xffc7, 0x006e, 0xfffc, 0x004c, 0x004c, 0x002a, 0x0056 },
	{ 0xffc6, 0xff8d, 0xfffc, 0xfffc, 0x0053, 0x0033, 0x0025, 0xff8c,    0xffe0, 0xfff1, 0xffdc, 0x002a, 0xffb7, 0x0063, 0xffa2, 0xffa9 },
	{ 0xffc4, 0x0014, 0xffbd, 0xfff4, 0x004c, 0xffc9, 0xff8b, 0x0044,    0x0052, 0x0050, 0xffa3, 0x0014, 0xffa4, 0x0015, 0x0080, 0x005b },
	{ 0x000b, 0xffac, 0x001c, 0xffb4, 0xffa2, 0x007c, 0xffdb, 0xffa3,    0xffef, 0x004e, 0x006a, 0x001d, 0xffa8, 0x000f, 0x002f, 0xff9a }
};


__attribute__((reqd_work_group_size(16, 4, 1)))
kernel void SIMD_16way(global uint *inputUINT, global uint *hashOut, global uchar *inputCHAR, constant short *alpha, constant ushort *beta) {
    inputCHAR += (get_global_id(1) - get_global_offset(1)) * 16 * 4;
	inputUINT += (get_global_id(1) - get_global_offset(1)) * 16;
	hashOut += (get_global_id(1) - get_global_offset(1)) * 16;
	
	// - - - - - - - - - - - MESSAGE EXPANSION - - - - - - - - - - -
	/* According to SIMD official documentation, this is the
		1st stage - "Number Theoric Transform"
	End of page 13. Be careful that our "alphas" are what they call "beta" as we are doing SIMD-512.
	
	In line of concept, every instance of 16 treads mangles a different hash and has a different, independant work.
	Every hash is computed 16-way so every WI has 16 values which are stored in the same LDS column.
	This is roughtly equal to "FFT16" in legacy kernels. */
	local int work[4 * 16 * 16];
	{
		const uint lx = get_local_id(0);
		uint start = 0;
		for(uint loop = 0; loop < 4; loop++) {
			uint mod = lx  % (2 << loop);
			uint add = mod / (1 << loop);
			start   += add * (8 >> loop);
		}
		int8 one = SIMD16W_MangleInput(inputCHAR, start);
		int8 two = SIMD16W_MangleInput(inputCHAR, start + 16);
		
		SIMD16W_PrimeLDS(work + LDSIDX(get_local_id(0), 0, get_local_id(1)), one, two);
	}
	for(uint loop = 0; loop < 4; loop++) {
		barrier(CLK_LOCAL_MEM_FENCE);
		SIMD16W_MergeIntervals(loop, work + LDSIDX(0, 0, get_local_id(1)), alpha);
	}
	
	// - - - - - - - - - - - CONCATENATED CODE - - - - - - - - - - -
	/* What legacy kernels do right now is clearly a an implementation of what reference.c
	implementation does at LN 155, followed by building the "concatenated code" of phase 2.
	I do it slightly differently, borrowing the ideas from the SIMD reference implementation.
	Note the reference implementation builds the beta value "on the fly" as the loop progresses.
	It's a quite smart thing to do if you're not concerned in multiple hashes.
	They just do %257. For us, that's fairly more involved.
	Always remember 1) there are 16 parallel "threads" here 2) I store consecutive work ints
	by column, therefore consecutive elements are really 16 elements apart.
	As a side note: SIMD vectorialized 16-way implementation might look like a perfect fit
	there but I don't quite understand it and I'm not even all that sure I want to use a LUT 
	for that. */
	{
		barrier(CLK_LOCAL_MEM_FENCE);
		local int *col = work + LDSIDX(get_local_id(0), 0, get_local_id(1));
		for(int row = 0; row < 16; row++) {
			int v = col[row * 32] + beta[get_local_id(0) * 16 + row];
			v = SIMD_ByteReduce(SIMD_ByteReduce(SIMD_ShortReduce(v)));
			col[row * 32] = v;
		}
		/* According to reference.c this lifts values to [-128, 128]
		I'm not pretty sure what "lift" means to them. SIMD documentation referres to
		those values being polynomials, so those polynomials might be raised to some power? */		
		for(int row = 0; row < 16; row++) {
			const int v = col[row * 32];
			col[row * 32] = v - (v > 128? 257 : 0);
		}
	}
	
	
	// - - - - - - - - - - - 3rd stage, PERMUTATION - - - - - - - - - - -	
	// Before the ladders, we build (A,B,C,D) = IV xor MSG
	// Our message is 64bytes <-> 256 bits, to be padded with 0s up to 512.
	// Because x xor 0 = x we have it slightly easier. Those values must also be in LDS.
	local int ABCD[4 * 4 * 8 + 1]; // +1 so it starts and ends on a different memory channel
	const uint vari = get_local_id(0) / 8, channel = get_local_id(0) % 8;
	// ABCD[0] = 0xDEADBEEF;
	ABCD[ABCDOFF(vari + 0, channel)] = SIMD_IV_512[vari + 0][channel] ^ inputUINT[get_local_id(0)];
	ABCD[ABCDOFF(vari + 2, channel)] = SIMD_IV_512[vari + 2][channel];
	barrier(CLK_LOCAL_MEM_FENCE);
	
	// Implementation detail: 8 WI needs to access work sequentially now. Avoid massive LDS bank
	// conflicts. So far I've been considerably more efficient than legacy implementations, but now
	// I start paying the price.
	WTranspose(work);
	
	// note: inner code expansion here would almost be embarassingly parallel!
	// TODO: inner code expansion!
	
	barrier(CLK_LOCAL_MEM_FENCE);
	
	// At this point, the 256 values in the matrix are to be considered 32 rows of 8 values each,
	// to be somehow "transformed" by the ABCD matrix. Those rows should be permuted.
	// It is just easier to permute the lookup address instead.
	
	/* Now we get to the real deal. 4 consecutive stages of 8 parallel Feistel block ciphers,
	building a Feistel network. It is easy to visualize them in their easiest form but you're
	better off to WikiPedia. Each block cipher basically mixes an an input with a constant and
	the result with another input. You can find the specific SIMD block cipher at page at
	page 15, and the full "message expansion" at page 17, also see SIMD reference.c:325 */
	SIMD16W_Round(ABCD, work, SIMD_ROROT[0], 0);
	SIMD16W_Round(ABCD, work, SIMD_ROROT[1], 1);
	SIMD16W_Round(ABCD, work, SIMD_ROROT[2], 2);
	SIMD16W_Round(ABCD, work, SIMD_ROROT[3], 3);
	
	// SIMD.pdf, page 19 (also see reference.c:350)
	// The whole compression function is made of 4 rounds, plus four final steps to mix
	// the initial chaining value to the initial state (this is our feed-forward).
	{
		uint4 mixin = get_local_id(0) < 8? (uint4)(0) : (uint4)(SIMD_IV_512[0][get_local_id(0) - 8],
                                                                SIMD_IV_512[1][get_local_id(0) - 8],
                                                                SIMD_IV_512[2][get_local_id(0) - 8],
                                                                SIMD_IV_512[3][get_local_id(0) - 8]);
		SIMD16W_Step(ABCD, 4, 0, mixin.s0,  4, 13, STEP_FUNC_IF);
		SIMD16W_Step(ABCD, 5, 0, mixin.s1, 13, 10, STEP_FUNC_IF);
		SIMD16W_Step(ABCD, 6, 0, mixin.s2, 10, 25, STEP_FUNC_IF);
		SIMD16W_Step(ABCD, 0, 0, mixin.s3, 25,  4, STEP_FUNC_IF);
	}
	
	// - - - - - - - - - - - Final compression function - - - - - - - - - - -
	// We don't do that. Our message is known to be 32 uints, so 128 bytes or 1024 bits.
	// Therefore, the work state is always the same: SIMD512_MESSAGE1024BIT_LAST_BLOCK_W
	// It's already in the format required by rounds for easy access.
	int4 abcdCopy = 0;
	if(get_local_id(0) >= 8) { // WARNING: same condition as step function
		abcdCopy.s0 = ABCD[ABCDOFF(0, get_local_id(0) - 8)];
		abcdCopy.s1 = ABCD[ABCDOFF(1, get_local_id(0) - 8)];
		abcdCopy.s2 = ABCD[ABCDOFF(2, get_local_id(0) - 8)];
		abcdCopy.s3 = ABCD[ABCDOFF(3, get_local_id(0) - 8)];
	}	
	if(get_local_id(0) == 0) ABCD[ABCDOFF(0, 0)] ^= 0x0200;
	{ // copy to local memory. Legacy kernels don't do that as they're MACRO based, but the compiler will likely arrange something anyway!
		local int *dst = work + LDSIDX(get_local_id(0), 0, get_local_id(1));
		for(uint i = 0; i < 16; i++) {
			*dst = SIMD512_MESSAGE1024BIT_LAST_BLOCK_W[i][get_local_id(0)];
			dst += 32; // one LDS line
		}
		barrier(CLK_LOCAL_MEM_FENCE);
	}
	SIMD16W_Round(ABCD, work, SIMD_ROROT[0], 0);
	SIMD16W_Round(ABCD, work, SIMD_ROROT[1], 1);
	SIMD16W_Round(ABCD, work, SIMD_ROROT[2], 2);
	SIMD16W_Round(ABCD, work, SIMD_ROROT[3], 3);
	SIMD16W_Step(ABCD, 4, 0, abcdCopy.s0,  4, 13, STEP_FUNC_IF);
	SIMD16W_Step(ABCD, 5, 0, abcdCopy.s1, 13, 10, STEP_FUNC_IF);
	SIMD16W_Step(ABCD, 6, 0, abcdCopy.s2, 10, 25, STEP_FUNC_IF);
	SIMD16W_Step(ABCD, 0, 0, abcdCopy.s3, 25,  4, STEP_FUNC_IF);
	
	hashOut[get_local_id(0)] = ABCD[ABCDOFF(get_local_id(0) / 8, get_local_id(0) % 8)];
}
