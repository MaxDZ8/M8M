#define B_STRIDE (1024 / sizeof(ulong))
#define V_STRIDE ((1024 * 1024 * 2) / sizeof(ulong))
#define S_STRIDE (2 * 256 * 2)
#define YESCRYPT_r 8


static constant const uint salsa20_simd_shuffle[64] = { // also include LE32DEC
    0x00, 0x01, 0x02, 0x03, 0x14, 0x15, 0x16, 0x17,
    0x28, 0x29, 0x2a, 0x2b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x10, 0x11, 0x12, 0x13, 0x24, 0x25, 0x26, 0x27,
    0x38, 0x39, 0x3a, 0x3b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x20, 0x21, 0x22, 0x23, 0x34, 0x35, 0x36, 0x37,
    0x08, 0x09, 0x0a, 0x0b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x30, 0x31, 0x32, 0x33, 0x04, 0x05, 0x06, 0x07,
    0x18, 0x19, 0x1a, 0x1b, 0x2c, 0x2d, 0x2e, 0x2f
};


static constant const uchar salsa20_simd_unshuffle[64] = { // also with LE32ENC
    0x00, 0x01, 0x02, 0x03, 0x34, 0x35, 0x36, 0x37,
    0x28, 0x29, 0x2a, 0x2b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x10, 0x11, 0x12, 0x13, 0x04, 0x05, 0x06, 0x07,
    0x38, 0x39, 0x3a, 0x3b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x20, 0x21, 0x22, 0x23, 0x14, 0x15, 0x16, 0x17,
    0x08, 0x09, 0x0a, 0x0b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x30, 0x31, 0x32, 0x33, 0x24, 0x25, 0x26, 0x27,
    0x18, 0x19, 0x1a, 0x1b, 0x0c, 0x0d, 0x0e, 0x0f
};

// For GBSTY, only the lower 32 bits are necessary
uint Integerify(local ulong *block, const uint r) {
	const local ulong *X = block + (2 * r - 1) * 8; // SALU FTW
	//const uint lo = (uint)(X[0]);
	//const uint hi = (uint)(X[6] >> 32);
	//return upsample(hi, lo);
	return (uint)(X[0]);
}


void blkxor(local ulong *block, global ulong *modifier, const uint s) {
	local uchar *collide = (local uchar*)block;
	global uchar *src = (global uchar*)modifier;
	for(uint loop = 0; loop < (s * sizeof(ulong)) / get_local_size(0); loop++) {
		collide[get_local_id(0)] ^= src[get_local_id(0)];
		collide += get_local_size(0);
		src += get_local_size(0);
	}
	barrier(CLK_LOCAL_MEM_FENCE);
}


void blkcpy(global ulong *dst, local ulong *src, const uint s) {
	local uchar *value = (local uchar*)src;
	global uchar *target = (global uchar*)dst;
	for(uint loop = 0; loop < (s * sizeof(ulong)) / get_local_size(0); loop++) {
		target[get_local_id(0)] = value[get_local_id(0)];
		target += get_local_size(0);
		value += get_local_size(0);
	}
	barrier(CLK_LOCAL_MEM_FENCE);
}


void Block_pwxform(local ulong *slice, global ulong *valS, local ulong *gather) {
	const size_t YESCRYPT_S_SIZE1 = 1 << 8;
	const size_t YESCRYPT_S_SIMD = 2;
	const size_t YESCRYPT_S_P = 4;
	const size_t YESCRYPT_S_ROUNDS = 6;
	const global uchar *so = (global uchar*)(valS);
	const global uchar *si = (global uchar*)(valS + YESCRYPT_S_SIZE1 * YESCRYPT_S_SIMD);
	slice += (get_local_id(0) / 16) * YESCRYPT_S_SIMD;
	gather += (get_local_id(0) / 16) * YESCRYPT_S_SIMD * 2;
	{
		ulong xo = slice[0];  // Bank conflict
		ulong xi = slice[1];  // Bank conflict
		for(uint round = 0; round < YESCRYPT_S_ROUNDS; round++) {
			const uint YESCRYPT_S_MASK = (YESCRYPT_S_SIZE1 - 1) * YESCRYPT_S_SIMD * 8;
			const ulong YESCRYPT_S_MASK2 = ((ulong)YESCRYPT_S_MASK << 32) | YESCRYPT_S_MASK;
			const ulong x = xo & YESCRYPT_S_MASK2;
			{ // unrolled formulation always uses good values
				const uint teamwork = get_local_id(0) % 16;
				local uchar *ubyte = (local uchar*)(gather);
				ubyte[teamwork] = so[(uint)(x) + teamwork];
				ubyte[teamwork + 16] = si[(uint)(x >> 32) + teamwork];
				barrier(CLK_LOCAL_MEM_FENCE);
			}
			xo = (ulong)(xo >> 32) * (uint)xo;
			xo += gather[0 + 0];
			xo ^= gather[2 + 0]; // do this uint for slightly improved perf?
			xi = (ulong)(xi >> 32) * (uint)xi;
			xi += gather[0 + 1];
			xi ^= gather[2 + 1];
		}
		slice[0] = xo;  // Bank conflict
		slice[1] = xi;  // Bank conflict
	}
}


void Salsa20_8(local ulong slice[8]) {
	uint inputValue = 0;
	local uint *words = (local uint*)(slice);
	if(get_local_id(0) < 16) inputValue = words[get_local_id(0)];
	barrier(CLK_LOCAL_MEM_FENCE);
	{
		local uchar *buff = (local uchar*)(slice);
		const uint shuffled = salsa20_simd_unshuffle[get_local_id(0)];
		uchar value = buff[shuffled];
		buff[get_local_id(0)] = value;
	}
	barrier(CLK_LOCAL_MEM_FENCE);
	for(uint round = 0; round < 8; round += 2) { // everything is SALU in there
		words[ 4] ^= rotate(words[ 0] + words[12],  7u);  words[ 8] ^= rotate(words[ 4] + words[ 0],  9u);
		words[12] ^= rotate(words[ 8] + words[ 4], 13u);  words[ 0] ^= rotate(words[12] + words[ 8], 18u);

		words[ 9] ^= rotate(words[ 5] + words[ 1],  7u);  words[13] ^= rotate(words[ 9] + words[ 5],  9u);
		words[ 1] ^= rotate(words[13] + words[ 9], 13u);  words[ 5] ^= rotate(words[ 1] + words[13], 18u);

		words[14] ^= rotate(words[10] + words[ 6],  7u);  words[ 2] ^= rotate(words[14] + words[10],  9u);
		words[ 6] ^= rotate(words[ 2] + words[14], 13u);  words[10] ^= rotate(words[ 6] + words[ 2], 18u);

		words[ 3] ^= rotate(words[15] + words[11], 7u);  words[ 7] ^= rotate(words[ 3] + words[15],  9u);
		words[11] ^= rotate(words[ 7] + words[ 3],13u);  words[15] ^= rotate(words[11] + words[ 7], 18u);

		/* Operate on rows */
		words[ 1] ^= rotate(words[ 0] + words[ 3],  7u);  words[ 2] ^= rotate(words[ 1] + words[ 0],  9u);
		words[ 3] ^= rotate(words[ 2] + words[ 1], 13u);  words[ 0] ^= rotate(words[ 3] + words[ 2], 18u);

		words[ 6] ^= rotate(words[ 5] + words[ 4],  7u);  words[ 7] ^= rotate(words[ 6] + words[ 5],  9u);
		words[ 4] ^= rotate(words[ 7] + words[ 6], 13u);  words[ 5] ^= rotate(words[ 4] + words[ 7], 18u);

		words[11] ^= rotate(words[10] + words[ 9],  7u);  words[ 8] ^= rotate(words[11] + words[10],  9u);
		words[ 9] ^= rotate(words[ 8] + words[11], 13u);  words[10] ^= rotate(words[ 9] + words[ 8], 18u);

		words[12] ^= rotate(words[15] + words[14],  7u);  words[13] ^= rotate(words[12] + words[15],  9u);
		words[14] ^= rotate(words[13] + words[12], 13u);  words[15] ^= rotate(words[14] + words[13], 18u);
	}
	{ // salsa20_simd_shuffle
		local uchar *buffer = (local uchar*)(slice);
		const uint shuffled = salsa20_simd_shuffle[get_local_id(0)];
		uchar value = buffer[shuffled];
		buffer[get_local_id(0)] = value;
	}
	if(get_local_id(0) < 16) words[get_local_id(0)] += inputValue;
	barrier(CLK_LOCAL_MEM_FENCE);
}


void Blockmix_pwxform(local ulong *block, global ulong *valS, local ulong *gather, const uint yescrypt_r1) { // Throw away VALU power like there's no tomorrow
	const size_t YESCRYPT_S_P_SIZE = 8;
	//const size_t yescrypt_r1 = YESCRYPT_r * 128 / (YESCRYPT_S_P_SIZE * 8);
	{
		local uint *slice = (local uint*)(block);
		local uint *modifier = (local uint*)(block + (yescrypt_r1 - 1) * YESCRYPT_S_P_SIZE);
		for(uint loop = get_local_id(0); loop < YESCRYPT_S_P_SIZE * 2; loop += get_local_size(0)) { // 2 uints per ulong
			slice[loop] ^= modifier[loop];
		}
	}
	barrier(CLK_LOCAL_MEM_FENCE);
	Block_pwxform(block, valS, gather);
	for(uint loop = 1; loop < yescrypt_r1; loop++) {
		{
			local uint *slice = (local uint*)(block + loop * YESCRYPT_S_P_SIZE);
			local uint *modifier = (local uint*)(block + (loop - 1) * YESCRYPT_S_P_SIZE);
			for(uint loop = get_local_id(0); loop < YESCRYPT_S_P_SIZE * 2; loop += get_local_size(0)) { // 2 uints per ulong
				slice[loop] ^= modifier[loop];
			}
			barrier(CLK_LOCAL_MEM_FENCE);
		}
		Block_pwxform(block + loop * YESCRYPT_S_P_SIZE, valS, gather);
	}
	const uint i = (yescrypt_r1 - 1) * YESCRYPT_S_P_SIZE / 8;
	Salsa20_8(block + i * 8);
}


/* This was originally written 16x4 ways, keeping 4 hashes in flight but it turns out Smix2 approach is just better,
let the hardware hide latency across different wavefronts instead of providing more instructions. */
__attribute__((reqd_work_group_size(64, 1, 1)))
kernel void SecondSmix1(global ulong *valB, global ulong *valV, global ulong *valS, const uint yescrypt_N, const uint yescrypt_r1) {
	const size_t slot = get_global_id(1) - get_global_offset(1);
	valV += slot * V_STRIDE;
	valS += slot * S_STRIDE;
	valB += slot * B_STRIDE;
	local ulong block[16 * 8];
	{ // there's a complication here: the previous stage outputs uints, not ulongs so adjust
		global uchar *src = (global uchar*)(valB);
		global uchar *dstv = (global uchar*)(valV);
		local uchar *dst = (local uchar*)block;
		const uint shuffled = salsa20_simd_shuffle[get_local_id(0)];
		const uint byteOrder = (get_local_id(0) / 4) * 4 + 3 - get_local_id(0) % 4;
		for(uint i = 0; i < 2 * YESCRYPT_r; i++) {
			const uchar value = src[shuffled];
			dst[get_local_id(0)] = value;
			dstv[byteOrder] = value;
			src += 8 * 8;
			dst += 8 * 8;
			dstv += 8 * 8;
		}
		barrier(CLK_LOCAL_MEM_FENCE);
		local uint *swz = (local uint*)block;
		for(uint i = get_local_id(0); i < 16 * 8 * 2; i += get_local_size(0)) {
			const uint value = swz[i];
			swz[i] = as_uint(as_uchar4(value).s3210);
		}
	}
	barrier(CLK_LOCAL_MEM_FENCE);
	local ulong gather[4 * 4];
	Blockmix_pwxform(block, valS, gather, yescrypt_r1);
	const uint yescrypt_s = 16 * YESCRYPT_r;
	{ // blkcpy
		global uchar *dst = (global uchar*)(valV + yescrypt_s);
		local uchar *src = (local uchar*)block;
		for(uint i = 0; i < 2 * YESCRYPT_r; i++) {
			dst[get_local_id(0)] = src[get_local_id(0)];
			src += 8 * 8;
			dst += 8 * 8;
		}
		barrier(CLK_LOCAL_MEM_FENCE);
	}
	// Now I am inside the big 'else' branch
	Blockmix_pwxform(block, valS, gather, yescrypt_r1);
	const bool yescrypt_rw = true;
	uint yescrypt_n = 1; // this is ulong in reference, but for GBSTY 32 bits are enough. Same applies to loop iterator
	for(uint yescrypt_i = 2; yescrypt_i < yescrypt_N; yescrypt_i++) { // note I do a single block per iteration, CPU does 2
		event_t copied = async_work_group_copy(valV + yescrypt_i * yescrypt_s, block, yescrypt_s, 0);
		wait_group_events(1, &copied);

		if(yescrypt_rw) {
			if((yescrypt_i % 2) == 0) { // this condition isn't in yescrypt, I need it as I do one block per iteration
				if((yescrypt_i & (yescrypt_i - 1)) == 0) yescrypt_n <<= 1;
			}
			uint j = Integerify(block, YESCRYPT_r);
			j &= yescrypt_n - 1;
			j += yescrypt_i - yescrypt_n;

			global const uint *indirected = (global uint*)(valV + j * yescrypt_s);
			local uint *dword = (local uint*)block;
			for(size_t el = 0; el < (yescrypt_s * 2) / 64; el++) {
				dword[get_local_id(0)] ^= indirected[get_local_id(0)];
				dword += get_local_size(0);
				indirected += get_local_size(0);
			}
			// ^ no LDS collisions and low instruction count BUT stride 4 bytes access
			// to be compared against packed 64byte read (uchar*)indirected, el<yescrypt_s*8;
			// Better bandwidth usage, which was supposed to be the problem here.
			barrier(CLK_LOCAL_MEM_FENCE);
		}
		Blockmix_pwxform(block, valS, gather, yescrypt_r1);
	}
	// for GBSTY parameters, _n is 0x20 at this point.
	{
		global uchar *dst = (global uchar*)(valB);
		local uchar *src = (local uchar*)(block);
		const uint shuffled = salsa20_simd_unshuffle[get_local_id(0)];
		for(uint i = 0; i < 2 * YESCRYPT_r; i++) {
			dst[get_local_id(0)] = src[shuffled];
			src += 8 * 8;
			dst += 8 * 8;
		}
	}
}

#ifdef YESCRYPT_RW
/* Smix2 is a bit different. It still revolves around doing integerify and xorring (but also updating) Vj blocks.
What I've seen in smix1 is that we need to be smarter, we cannot keep on iterating host-side.
Plus, I really want to maximize cache usage and latency hiding to have nearly free global memory barriers...
... so I try something different. Instead of 16-way, I go even crazier with 64-way. SALU FTW */
__attribute__((reqd_work_group_size(64, 1, 1))) // a full wavefront to mangle an hash
kernel void Smix2(global ulong *valB, global ulong *valV, global ulong *valS, const uint yescrypt_NLoop, const uint yescrypt_r1) {
	const uint YESCRYPT_s = YESCRYPT_r * 16;
	//const uint yescrypt_NLoop = 682; // for first call
	//                          = 2; // for second call
	//const bool YESCRYPT_RW = 1; // for first call
	//                         0; // for second call
	const uint YESCRYPT_N = 2048;
	const size_t slot = get_global_id(1) - get_global_offset(1);
	valB += slot * B_STRIDE;
	valV += slot * V_STRIDE;
	valS += slot * S_STRIDE;
	local ulong block[16 * 8];
	local ulong gather[4 * 4];

	{
		global uchar *src = (global uchar*)(valB);
		local uchar *dst = (local uchar*)block;
		const uint shuffled = salsa20_simd_shuffle[get_local_id(0)];
		for(uint i = 0; i < 2 * YESCRYPT_r; i++) {
			dst[get_local_id(0)] = src[shuffled];
			src += 8 * 8;
			dst += 8 * 8;
		}
	}
	barrier(CLK_LOCAL_MEM_FENCE);
	for(uint loop = 0; loop < yescrypt_NLoop; loop++) {
		uint j = Integerify(block, YESCRYPT_r);
		j &= (YESCRYPT_N - 1); // modulo pow2
		blkxor(block, valV + j * YESCRYPT_s, YESCRYPT_s);
		if(YESCRYPT_RW) blkcpy(valV + j * YESCRYPT_s, block, YESCRYPT_s);
		Blockmix_pwxform(block, valS, gather, yescrypt_r1);
		barrier(CLK_GLOBAL_MEM_FENCE);
	}
	{
		global uchar *dst = (global uchar*)(valB);
		local uchar *src = (local uchar*)(block);
		const uint shuffled = salsa20_simd_unshuffle[get_local_id(0)];
		for(uint i = 0; i < 2 * YESCRYPT_r; i++) {
			dst[get_local_id(0)] = src[shuffled];
			src += 8 * 8;
			dst += 8 * 8;
		}
	}
}
#endif
