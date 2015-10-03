// Always the typical SHA256 functions...
uint Ch(uint x, uint y, uint z) { return (x & (y ^ z)) ^ z; }
uint Maj(uint x, uint y, uint z) { return (x & (y | z)) | (y & z); }
uint ROTR(uint x, uint n) { return rotate(x, 32 - n); }
uint S0(uint x) { return ROTR(x,  2) ^ ROTR(x, 13) ^ ROTR(x, 22); }
uint S1(uint x) { return ROTR(x,  6) ^ ROTR(x, 11) ^ ROTR(x, 25); }
uint s0(uint x) { return ROTR(x,  7) ^ ROTR(x, 18) ^ (x >> 3); }
uint s1(uint x) { return ROTR(x, 17) ^ ROTR(x, 19) ^ (x >> 10); }


/*! The simplest SHA256 round, taken from groestl-myr implementation.
Shall I consolidate those? Not yet, as there are several variants...  */
void SHARound_Set(uint8 *v, uint *w, constant uint *k) {
	uint8 vals = *v;
	#pragma unroll
	for(uint i = 0; i < 16; i++) {
		uint t0 = vals.s7 + S1(vals.s4) + Ch(vals.s4, vals.s5, vals.s6) + k[i] + w[i];
		uint t1 = S0(vals.s0) + Maj(vals.s0, vals.s1, vals.s2);
		vals.s3 += t0;
		vals.s7 = t0 + t1;
		vals = vals.s70123456; // hopefully the compiler unrolls this for me
	}
	*v = vals;
}


constant uint K[4][16] = {
	{
		0x428A2F98, 0x71374491,    0xB5C0FBCF, 0xE9B5DBA5,
	    0x3956C25B, 0x59F111F1,    0x923F82A4, 0xAB1C5ED5,
		0xD807AA98, 0x12835B01,    0x243185BE, 0x550C7DC3,
		0x72BE5D74, 0x80DEB1FE,    0x9BDC06A7, 0xC19BF174
	},
	{
		0xE49B69C1, 0xEFBE4786,    0x0FC19DC6, 0x240CA1CC,
	    0x2DE92C6F, 0x4A7484AA,    0x5CB0A9DC, 0x76F988DA,
		0x983E5152, 0xA831C66D,    0xB00327C8, 0xBF597FC7,
	    0xC6E00BF3, 0xD5A79147,    0x06CA6351, 0x14292967
	},
	{
		0x27B70A85, 0x2E1B2138,    0x4D2C6DFC, 0x53380D13,
		0x650A7354, 0x766A0ABB,    0x81C2C92E, 0x92722C85,
		0xA2BFE8A1, 0xA81A664B,    0xC24B8B70, 0xC76C51A3,
	    0xD192E819, 0xD6990624,    0xF40E3585, 0x106AA070
	},
	{
		0x19A4C116, 0x1E376C08,    0x2748774C, 0x34B0BCB5,
	    0x391C0CB3, 0x4ED8AA4A,    0x5B9CCA4F, 0x682E6FF3,
		0x748F82EE, 0x78A5636F,    0x84C87814, 0x8CC70208,
		0x90BEFFFA, 0xA4506CEB,    0xBEF9A3F7, 0xC67178F2
	}
};


void SHA256_transform(uint8 *state, uint *block) {
	uint work[64];
	for(uint i = 0; i < 16; i++) work[i] = block[i];
	for(uint i = 16; i < 64; i++) {
		work[i] = s1(work[i -  2]) + work[i -  7] +
		          s0(work[i - 15]) + work[i - 16];
	}
	uint8 sched = *state;
	for(uint i = 0; i < 4; i++) SHARound_Set(&sched, work + i * 16, K[i]);
	*state += sched;
}


uint8 SHA256_IV() {
	return (uint8)(0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
	               0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19);
}


kernel void yescrypt_sha256_80B(global uint *block, global uint *hashOut) {
	const uint slot = get_global_id(0) - get_global_offset(0);
	uint8 state = SHA256_IV();
	uint shaBuff[16];
	for(uint i = 0; i < 16; i++) shaBuff[i] = as_uint(as_uchar4(block[i]).wzyx);
	SHA256_transform(&state, shaBuff);
	for(uint i = 0; i < 3; i++) shaBuff[i] = as_uint(as_uchar4(block[i + 16]).wzyx);
	shaBuff[3] = (uint)(get_global_id(0)); // nonce
	shaBuff[4] = 0x80000000;
	shaBuff[5] = 0;
	shaBuff[6] = shaBuff[7] = 0;
	shaBuff[8] = shaBuff[9] = 0;
	shaBuff[10] = shaBuff[11] = 0;
	shaBuff[12] = shaBuff[13] = 0;
	shaBuff[14] = shaBuff[14] = 0;
	shaBuff[15] = 80 * 8;
	SHA256_transform(&state, shaBuff);
	hashOut += slot * 8;
	hashOut[0] = state.s0;
	hashOut[1] = state.s1;
	hashOut[2] = state.s2;
	hashOut[3] = state.s3;
	hashOut[4] = state.s4;
	hashOut[5] = state.s5;
	hashOut[6] = state.s6;
	hashOut[7] = state.s7;
}




#ifdef PBKDF2_BLOCK_STRIDE
#ifndef PBKDF2_PREVHASH_STRIDE
  #error "PBKDF2_BLOCK_STRIDE defined but PBKDF2_PREVHASH_STRIDE is not."
#else

/*! specific to yescrypt being GPU'ed.
First parameter will be hash computed by yescrypt_sha256_80B, while second will be the same
input given as block to yescrypt_sha256_80B.
Output is two SHA256 states. The first state octx will have mangled 512 bit, the second 1152.
Outputs are packed, with value[i+1] for a WI being successive to value[i+0] for the same WI.
"octx" comes first, followed by "ictx" for the same hash.
This happens because a later step is 32-way parallel and I want those to be simple. */
kernel void PBKDF2SHA256_init(global uint *prevHash, global uint *block,
                               global uint *oictx) {
	const uint slot = get_global_id(0) - get_global_offset(0);
	prevHash += slot * PBKDF2_PREVHASH_STRIDE;
	oictx += slot * 16;
	block += slot * PBKDF2_BLOCK_STRIDE;
	uint8 state = SHA256_IV();
	uint shaBuff[16];
	for(uint i = 0; i < 8; i++) {
		uint val = prevHash[i];
		#ifdef SWAP_PREVHASH_BYTES
		val = as_uint(as_uchar4(val).wzyx);
		#endif
		shaBuff[i] = val ^ 0x36363636;
		shaBuff[i + 8] = 0x36363636;
	}
	SHA256_transform(&state, shaBuff);
	uint8 ictx = state;
	state = SHA256_IV();
	for(uint i = 0; i < 8; i++) {
		uint val = prevHash[i];
		#ifdef SWAP_PREVHASH_BYTES
		val = as_uint(as_uchar4(val).wzyx);
		#endif
		shaBuff[i] = val ^ 0x5c5c5c5c;
		shaBuff[i + 8] = 0x5c5c5c5c;
	}
	SHA256_transform(&state, shaBuff);
	uint8 octx = state;
	oictx[0 + 0] = octx.s0;
	oictx[1 + 0] = octx.s1;
	oictx[2 + 0] = octx.s2;
	oictx[3 + 0] = octx.s3;
	oictx[4 + 0] = octx.s4;
	oictx[5 + 0] = octx.s5;
	oictx[6 + 0] = octx.s6;
	oictx[7 + 0] = octx.s7;
	
	// ^^ the above went by the name of "HMAC_SHA256_Init_Y" in original source
	// vv now let's add the salt, which is the original block. "HMAC_SHA256_Update_Y"
	// This is very simple. Using ictx as a context, mangle the first 64 bytes block
	// ... from the block.
	// Unless this is the second PBKDF call, then consume all the 64-byte blocks.
	const uint numBlocks = PBKDF2_BLOCK_STRIDE == 0? 1 : (PBKDF2_BLOCK_STRIDE / 16);
	for(uint mangle = 0; mangle < numBlocks; mangle++) {
		for(uint i = 0; i < 16; i++) shaBuff[i] = as_uint(as_uchar4(block[i]).wzyx);
		SHA256_transform(&ictx, shaBuff);
		block += 16;
	}
	oictx[0 + 8] = ictx.s0;
	oictx[1 + 8] = ictx.s1;
	oictx[2 + 8] = ictx.s2;
	oictx[3 + 8] = ictx.s3;
	oictx[4 + 8] = ictx.s4;
	oictx[5 + 8] = ictx.s5;
	oictx[6 + 8] = ictx.s6;
	oictx[7 + 8] = ictx.s7;
}


uint8 LoadLDS(local uint *src) {
	uint8 ret;
	ret.s0 = src[0];
	ret.s1 = src[1];
	ret.s2 = src[2];
	ret.s3 = src[3];
	ret.s4 = src[4];
	ret.s5 = src[5];
	ret.s6 = src[6];
	ret.s7 = src[7];
	return ret;
}


/*! To finish what's called "PBKDF2_SHA256" in yeScrypt implementation I'm reproducing, there's a loop filling
blocks of 32 bytes. It has to fill a 1024 byte buffer so that's 32 iterations.
I do it fairly differently instead: the loops always start from octx,ictx produced with PBKDF2SHA256_init so they are
completely parallel. I originally intended to do this 16-way and have each WI process blocks n,n+16 (to give them some
less register pressure) but due to memory concerns I think it's better to keep those packed as closely as possible. */
__attribute__((reqd_work_group_size(32, 2, 1)))
kernel void PBKDF2SHA256_expand_32W(global uint *oictx_gl, global uint *sha256, global uint *block, global uint *keyOut) {
	const uint slot = get_global_id(1) - get_global_offset(1);
	oictx_gl += (slot / 2) * 2 * (8 + 8);
	// ok friends, I need to load 16 uints so we can exploit LDS broadcast... three times in fact.
	local uint oictx_lds[16 * 2], lastBlock_lds[4];
	local uint *oictx = oictx_lds + 16 * get_local_id(1);
	{
		event_t wait = async_work_group_copy (oictx_lds, oictx_gl, 16 * 2, 0);
		wait = async_work_group_copy(lastBlock_lds + 0, block + 16, 4, wait); // ouch ouch
		wait_group_events(1, &wait);
	}
	uint8 octx = LoadLDS(oictx + 0);
	uint8 ictx = LoadLDS(oictx + 8);
	
	uint shaBuff[16];
	for(uint i = 0; i < 3; i++) shaBuff[i] = as_uint(as_uchar4(lastBlock_lds[i]).wzyx);
	shaBuff[3] = (uint)get_global_id(1); // nonce, again
	{ // PadInnerBlock(shaBuff);
		// it is my understanding that was supposed to be BE... (?)
		shaBuff[4] = get_local_id(0) + 1; // unrolled across WIs with same local-y
		shaBuff[5] = 0x80000000;
		for(uint i = 6; i < 15; i++) shaBuff[i] = 0;
		shaBuff[15] = 0x000004a0;
	}
	SHA256_transform(&ictx, shaBuff);
	// now, state is "ihash" going in at sha256_Y:347.
	// Being concatenated to previous block, which is empty.
	shaBuff[0] = ictx.s0;
	shaBuff[1] = ictx.s1;
	shaBuff[2] = ictx.s2;
	shaBuff[3] = ictx.s3;
	shaBuff[4] = ictx.s4;
	shaBuff[5] = ictx.s5;
	shaBuff[6] = ictx.s6;
	shaBuff[7] = ictx.s7;
	{ // PadOuterBlock(shaBuff), of course those magic numbers are precomputed.
		shaBuff[ 8] = 0x80000000;
		for(uint i = 9; i < 15; i++) shaBuff[i] = 0;
		shaBuff[15] = 0x00000300;
	}
	SHA256_transform(&octx, shaBuff);
	
	// Now, this hash is sometimes called "U", sha256_Y:392.
	keyOut += (slot * 32 + get_local_id(0)) * 8;
	keyOut[0] = octx.s0;
	keyOut[1] = octx.s1;
	keyOut[2] = octx.s2;
	keyOut[3] = octx.s3;
	keyOut[4] = octx.s4;
	keyOut[5] = octx.s5;
	keyOut[6] = octx.s6;
	keyOut[7] = octx.s7;
	if(get_local_id(0) == 0) { // in yescrypt.c this really only happens if(t || flags), true for GlobalBoost-Y
		sha256 += slot * 8;
		sha256[0] = as_uint(as_uchar4(octx.s0).wzyx);
		sha256[1] = as_uint(as_uchar4(octx.s1).wzyx);
		sha256[2] = as_uint(as_uchar4(octx.s2).wzyx);
		sha256[3] = as_uint(as_uchar4(octx.s3).wzyx);
		sha256[4] = as_uint(as_uchar4(octx.s4).wzyx);
		sha256[5] = as_uint(as_uchar4(octx.s5).wzyx);
		sha256[6] = as_uint(as_uchar4(octx.s6).wzyx);
		sha256[7] = as_uint(as_uchar4(octx.s7).wzyx);
	}
}

#endif
#endif


/*! The second call to PBKDF2_SHA256 must produce 32 bytes output instead of 1024.
It is basically the same as before but using different data and no parallelism so I redo it (instead of attempting something
with ifdefs).
This is meant to be instantiated 1D, one WI each hash. */
kernel void PBKDF2SHA256_last(global uint *oictx) {
	const uint slot = get_global_id(0) - get_global_offset(0);
	oictx += slot * (8 + 8);
	uint8 octx = vload8(0, oictx);
	uint8 ictx = vload8(1, oictx);
	uint shaBuff[16];
	{ // PadInnerBlock(shaBuff);
		shaBuff[0] = 0x01000000; // those are the values you could observe in CPU code through a debugger
		shaBuff[1] = 0x00000080;
		for(uint i = 2; i < 15; i++) shaBuff[i] = 0;
		shaBuff[15] = 0x20220000;
		
		shaBuff[0] = as_uint(as_uchar4(shaBuff[0]).wzyx);
		shaBuff[1] = as_uint(as_uchar4(shaBuff[1]).wzyx);
		shaBuff[15] = as_uint(as_uchar4(shaBuff[15]).wzyx);
	}
	SHA256_transform(&ictx, shaBuff);
	// now, state is "ihash" going in at sha256_Y:347.
	// Being concatenated to previous block, which is empty.
	shaBuff[0] = as_uint(as_uchar4(ictx.s0).wzyx);
	shaBuff[1] = as_uint(as_uchar4(ictx.s1).wzyx);
	shaBuff[2] = as_uint(as_uchar4(ictx.s2).wzyx);
	shaBuff[3] = as_uint(as_uchar4(ictx.s3).wzyx);
	shaBuff[4] = as_uint(as_uchar4(ictx.s4).wzyx);
	shaBuff[5] = as_uint(as_uchar4(ictx.s5).wzyx);
	shaBuff[6] = as_uint(as_uchar4(ictx.s6).wzyx);
	shaBuff[7] = as_uint(as_uchar4(ictx.s7).wzyx);
	{ // PadOuterBlock(shaBuff), of course those magic numbers are precomputed.
		shaBuff[ 8] = 0x80000000;
		for(uint i = 9; i < 15; i++) shaBuff[i] = 0;
		shaBuff[15] = 0x00000300;
		
		shaBuff[8] = as_uint(as_uchar4(shaBuff[8]).wzyx);
		shaBuff[15] = as_uint(as_uchar4(shaBuff[15]).wzyx);
	}
	for(uint i = 0; i < 16; i++) shaBuff[i] = as_uint(as_uchar4(shaBuff[i]).wzyx);
	SHA256_transform(&octx, shaBuff);
	oictx -= slot * 8;
	vstore8(octx, 0, oictx);
}


kernel void FinalMangling(global uint *io, global uint *block
#ifdef NONCE_SELECTION
	, global volatile uint *found, global uint *dispatchData
#endif
) {
	const size_t slot = get_global_id(0) - get_global_offset(0);
	io += slot * 8;
	// See PBKDF2SHA256_init
	uint8 ictx = SHA256_IV();
	uint shaBuff[16];
	for(uint i = 0; i < 8; i++) {
		shaBuff[i] = io[i] ^ 0x36363636;
		shaBuff[i + 8] = 0x36363636;
	}
	SHA256_transform(&ictx, shaBuff);
	uint8 octx = SHA256_IV();
	for(uint i = 0; i < 8; i++) {
		shaBuff[i] = io[i] ^ 0x5c5c5c5c;
		shaBuff[i + 8] = 0x5c5c5c5c;
	}
	SHA256_transform(&octx, shaBuff);
	for(uint i = 0; i < 16; i++) shaBuff[i] = as_uint(as_uchar4(block[i]).wzyx);
	SHA256_transform(&ictx, shaBuff);
	for(uint i = 0; i < 3; i++) shaBuff[i] = as_uint(as_uchar4(block[16 + i]).wzyx);
	shaBuff[3] = get_global_id(0);
	{ // pad
		shaBuff[4] = 0x00000080;
		for(uint i = 5; i < 15; i++) shaBuff[i] = 0;
		shaBuff[15] = 0x80040000;
		
		shaBuff[4] = as_uint(as_uchar4(shaBuff[4]).wzyx);
		shaBuff[15] = as_uint(as_uchar4(shaBuff[15]).wzyx);
	}
	SHA256_transform(&ictx, shaBuff);
	shaBuff[0] = ictx.s0;
	shaBuff[1] = ictx.s1;
	shaBuff[2] = ictx.s2;
	shaBuff[3] = ictx.s3;
	shaBuff[4] = ictx.s4;
	shaBuff[5] = ictx.s5;
	shaBuff[6] = ictx.s6;
	shaBuff[7] = ictx.s7;
	{ // pad
		shaBuff[ 8] = as_uint(as_uchar4(0x00000080).wzyx);
		shaBuff[ 9] = as_uint(as_uchar4(0x00000000).wzyx);
		shaBuff[10] = as_uint(as_uchar4(0x00000000).wzyx);
		shaBuff[11] = as_uint(as_uchar4(0x00000000).wzyx);
		shaBuff[12] = as_uint(as_uchar4(0x00000000).wzyx);
		shaBuff[13] = as_uint(as_uchar4(0x00000000).wzyx);
		shaBuff[14] = as_uint(as_uchar4(0x00000000).wzyx);
		shaBuff[15] = as_uint(as_uchar4(0x00030000).wzyx);
	}
	SHA256_transform(&octx, shaBuff);
	shaBuff[0] = octx.s0;
	shaBuff[1] = octx.s1;
	shaBuff[2] = octx.s2;
	shaBuff[3] = octx.s3;
	shaBuff[4] = octx.s4;
	shaBuff[5] = octx.s5;
	shaBuff[6] = octx.s6;
	shaBuff[7] = octx.s7;
	{ // pad
		shaBuff[ 8] = as_uint(as_uchar4(0x00000080).wzyx);
		shaBuff[ 9] = as_uint(as_uchar4(0x00000000).wzyx);
		shaBuff[10] = as_uint(as_uchar4(0x00000000).wzyx);
		shaBuff[11] = as_uint(as_uchar4(0x00000000).wzyx);
		shaBuff[12] = as_uint(as_uchar4(0x00000000).wzyx);
		shaBuff[13] = as_uint(as_uchar4(0x00000000).wzyx);
		shaBuff[14] = as_uint(as_uchar4(0x00000000).wzyx);
		shaBuff[15] = as_uint(as_uchar4(0x00010000).wzyx);
	}
	octx = SHA256_IV();
	SHA256_transform(&octx, shaBuff);
	#ifdef NONCE_SELECTION
	const uint magic = as_uint(as_uchar4(octx.s7).wzyx);
	const uint target = dispatchData[1];
	if(magic <= target) {
        const uint storage = atomic_inc(found);
		found++;
		found += storage * 9;
		found[0] = get_global_id(0);
		found[1] = as_uint(as_uchar4(octx.s0).wzyx);
		found[2] = as_uint(as_uchar4(octx.s1).wzyx);
		found[3] = as_uint(as_uchar4(octx.s2).wzyx);
		found[4] = as_uint(as_uchar4(octx.s3).wzyx);
		found[5] = as_uint(as_uchar4(octx.s4).wzyx);
		found[6] = as_uint(as_uchar4(octx.s5).wzyx);
		found[7] = as_uint(as_uchar4(octx.s6).wzyx);
		found[8] = as_uint(as_uchar4(octx.s7).wzyx);
	}
	#else // to next step of hash chain / "original" yescrypt hash
	io[0] = as_uint(as_uchar4(octx.s0).wzyx);
	io[1] = as_uint(as_uchar4(octx.s1).wzyx);
	io[2] = as_uint(as_uchar4(octx.s2).wzyx);
	io[3] = as_uint(as_uchar4(octx.s3).wzyx);
	io[4] = as_uint(as_uchar4(octx.s4).wzyx);
	io[5] = as_uint(as_uchar4(octx.s5).wzyx);
	io[6] = as_uint(as_uchar4(octx.s6).wzyx);
	io[7] = as_uint(as_uchar4(octx.s7).wzyx);
	#endif
}
