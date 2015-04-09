/*
 * Copyright (C) 2014 Massimo Del Zotto
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
/*
Neoscrypt. First thing to do is to produce a 256 byte value
X = FastKDF(blockheader, len=80, N=32).
This FastKDF features a key schedule so it needs to be sequential. Meh.
For the rest, sequential kernels use byte offsets, which requires quite some mangling.

An important difference is that while other kernels are "sort of" correct-ish, I cut it
really short in this one so for example I exploit the fact data blocks are known in size
and avoid _update calls, producing hashblocks directly.
*/

// In theory I could just use as_uchar4 but in practice order of bytes is not guarenteed to be
// portable so I have to do this and hope it's the same as the hardware operation.
uchar4 CharFour(uint val) {
	uchar4 ret;
	ret.w = (uchar)( val        & 0xFF);
	ret.z = (uchar)((val >>  8) & 0xFF);
	ret.y = (uchar)((val >> 16) & 0xFF);
	ret.x = (uchar)((val >> 24) & 0xFF);
	return ret;
}


uint LoadUint(global uchar *ptr, uint index) {
	uchar4 temp = vload4(index, ptr);
	ushort hi = upsample(temp.y, temp.x); // hi, lo, but endianess applies
	ushort lo = upsample(temp.w, temp.z);
	return upsample(lo, hi);
}


uint SumBytes(uint *val) {
	uint acc = 0;
	for(uint i = 0; i < 2; i++) {
		uchar4 temp = as_uchar4(val[i]);
		acc += temp.x;
		acc += temp.y;
		acc += temp.z;
		acc += temp.w;
	}
	return acc;
}


constant uint blake2S_IV[8] = {
    0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
    0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
};

constant uchar blake2S_sigma[10][16] = {
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 } ,
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 } ,
    { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 } ,
    {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 } ,
    {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 } ,
    {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 } ,
    { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 } ,
    { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 } ,
    {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 } ,
    { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13 , 0 } ,
};


uint ROTR32(uint val, uint amount) { return rotate(val, 32u - amount); }


/*! The blake transform is expected to work with a very simple memory layout:
val is a temporary buffer. Its data is born ad dies inside here. It's really a local, but since
LDS cannot be declared locally, it is passed here. It is the blake state to be accessed by column and
by diagonal.
There's another data block msg to be permuted by sigma depending on the round being mangled.

In general, val needs fast read-write, while msg is really read-only by this function.

Both buffers are accessed linearly, so they must be uint[16].

val is initially filled by the state, which is packed across different WIs in the same local-y by column. */
uint2 Blake2SBlockXForm_4way(uint2 hash, uint counter, const uint numRounds, local uint *val, const local uint *msg) {
	val[get_local_id(0) +  0] = hash.x;
	val[get_local_id(0) +  4] = hash.y;
	val[get_local_id(0) +  8] = 0;
	val[get_local_id(0) + 12] = counter; // this is t[0,1] or f[0,1] for g_l_id(0)=0123
	barrier(CLK_LOCAL_MEM_FENCE);
	val[get_local_id(0) +  8] ^= blake2S_IV[get_local_id(0) + 0];
	val[get_local_id(0) + 12] ^= blake2S_IV[get_local_id(0) + 4];
	barrier(CLK_LOCAL_MEM_FENCE);
	for(uint round = 0; round < numRounds; round++) {
		for(uint inner = 0; inner < 2; inner++) {
			uint v[4];
			if(inner == 0) { // column pass
				for(uint cp = 0; cp < 4; cp++) v[cp] = val[get_local_id(0) + cp * 4];
			}
			else { // diagonal pass
				for(uint cp = 0; cp < 4; cp++) {
					const uint srci = (get_local_id(0) + cp) % 4;
					v[cp] = val[srci + cp * 4];
				}
			}
			v[0] = v[0] + v[1] + msg[blake2S_sigma[round][inner * 8 + get_local_id(0) * 2 + 0]];
			v[3] = ROTR32(v[3] ^ v[0], 16);
			v[2] = v[2] + v[3];
			v[1] = ROTR32(v[1] ^ v[2], 12);
			v[0] = v[0] + v[1] + msg[blake2S_sigma[round][inner * 8 + get_local_id(0) * 2 + 1]];
			v[3] = ROTR32(v[3] ^ v[0], 8);
			v[2] = v[2] + v[3];
			v[1] = ROTR32(v[1] ^ v[2], 7);
			if(inner == 0) { // column pass
				for(uint cp = 0; cp < 4; cp++) val[get_local_id(0) + cp * 4] = v[cp];
			}
			else { // diagonal pass
				for(uint cp = 0; cp < 4; cp++) {
					const uint srci = (get_local_id(0) + cp) % 4;
					val[srci + cp * 4] = v[cp];
				}
			}
		}
		barrier(CLK_LOCAL_MEM_FENCE);
	}
	hash.x ^= val[get_local_id(0) + 0] ^ val[get_local_id(0) + 0 + 8];
	hash.y ^= val[get_local_id(0) + 4] ^ val[get_local_id(0) + 4 + 8];
	return hash;
}


void Blake2S_64_32_4way(local uint *lds, uint *output, uint *input, uint *key, const uint numRounds) {
	uint2 hashSlice;
	hashSlice.x = blake2S_IV[get_local_id(0) + 0];
	hashSlice.y = blake2S_IV[get_local_id(0) + 4];
	if(get_local_id(0) == 0) {
		const uint outSZ = sizeof(uint) * 8, keySZ = sizeof(uint) * 8;
		hashSlice.x ^= outSZ | (keySZ << 8) | (1 << 16) | (1 << 24);
	}
	// All threads collaborate in setting up the message.
	local uint *val = lds;
	local uint *block = val + 16;
	for(uint cp =  0; cp < 2; cp++) block[get_local_id(0) + cp * 4] = key[cp];
	for(uint cp =  2; cp < 4; cp++) block[get_local_id(0) + cp * 4] = 0;
	uint counter = get_local_id(0) == 0? 64 : 0;
	hashSlice = Blake2SBlockXForm_4way(hashSlice, counter, numRounds, val, block);
	if(get_local_id(0) == 0) counter = 128;
	else if(get_local_id(0) == 2) counter = ~0;
	for(uint cp = 0; cp < 4; cp++) block[get_local_id(0) + cp * 4] = input[cp];
	hashSlice = Blake2SBlockXForm_4way(hashSlice, counter, numRounds, val, block);

	output[0] = hashSlice.x;
	output[1] = hashSlice.y;
}


void PartialCopy(global uchar *dst, uint value, uint byteCount) {
	byteCount = min(byteCount, 4u);
	for(uint cp = 0; cp < byteCount; cp++) dst[cp] = (uchar)(value >> (8 * cp));
	// TODO byte order issue here!
}


// Buffers mangled by FastKDF take this amount of bytes... plus some.
#define KDF_SIZE 256u


uint FastKDFIteration(local uint *lds, uint buffStart, global uchar *buff_a, global uchar *buff_b) {
	uint input[4], key[2];
	for(uint cp = 0; cp < 4; cp++) input[cp] = LoadUint(buff_a + buffStart, cp * 4 + get_local_id(0));
	for(uint cp = 0; cp < 2; cp++) key[cp] = LoadUint(buff_b + buffStart, cp * 4 + get_local_id(0));
	local uint *team = lds + get_local_id(1) * 33;
	local uint *sum = team + 32;
	if(get_local_id(0) == 0) *sum = 0;

	uint prf_output[2];
	Blake2S_64_32_4way(team, prf_output, input, key, 10);
	barrier(CLK_LOCAL_MEM_FENCE);
	atomic_add(sum, SumBytes(prf_output));
	barrier(CLK_LOCAL_MEM_FENCE); // no real need to do that with atomics
	buffStart = *sum;
	buffStart %= KDF_SIZE; // or &= (256 - 1), the same
	// Now some more shit. Modify the salt buffer and store it back in place.
	for(uint cp = 0; cp < 2; cp++) {
		uint bval = LoadUint(buff_b + buffStart, cp * 4 + get_local_id(0));
		bval ^= prf_output[cp];
		input[cp] = bval; // Keep this around in case of need later.
		vstore4(CharFour(bval).wzyx, cp * 4 + get_local_id(0), buff_b + buffStart);
	}
	/* There are two cases here: either move the new values forward or
	move back some uints. Those are usually about 25% chance, with the former
	slightly more likely. In 75% of the cases there will be nothing to do at all.
	In case there's something to do, given current parameters is either forward
	or backward but never both. However, because we might have overwritten the end
	of the KDF buffer previously we need to put a memory barrier before moving back. */
	if(buffStart < sizeof(key) * 4) {
		uint count = min(sizeof(prf_output) * 4, sizeof(key) * 4 - buffStart);
		/* Forward 256 bytes the values I just wrote.
		The original kernel reads memory and writes memory: I have saved stuff in registers,
		so I can defer memory flushes a little bit.
		There is sure no race condition over those values as the memory range is different.
		Small complication: copy part of a 32-bit values.
		Further complication for 4-way version: write out in chunks and branch on need.
		Put the two things together: it's just easier to pull bytes on need. */
		global uchar *base = buff_b + buffStart + KDF_SIZE;
		base += get_local_id(0) * 4;
		count -= min(count, get_local_id(0) * 4);
		for(uint cp = 0; cp < 2 && count; cp++) {
			PartialCopy(base + cp * 4u * 4u, input[cp], count);
			count -= min(count, 4u * 4u);
		}
	}
	// todo: maybe I could use atomics to do this only on need,
	// if(some_atomic != 0) barrier(GLOBAL).
	barrier(CLK_GLOBAL_MEM_FENCE); // being B 320B in size, we might be able to afford this in LDS if I split 4-way
	uint rem = KDF_SIZE - buffStart;
	if(rem < sizeof(prf_output) * 4) { // memory-to-memory, no swizzling, copy bytes directly here
		uint count = sizeof(prf_output) * 4 - rem;
		global uchar *src = buff_b + KDF_SIZE;
		global uchar *dst = buff_b;
		for(uint cp = get_local_id(0); cp < count; cp += get_local_size(0)) {
			uchar val = src[cp];
			dst[cp] = val;
		}
	}
	return buffStart;
}


/* Pattern assumed to be 20 uints, all WIs collaborate. */
void FillInitialBuffer(global uchar *target, size_t extraBytes, local uint *pattern) {
	const uint blockLen = 20;
	const uint fullBlocks = KDF_SIZE / (blockLen * 4);
	const uint remaining = KDF_SIZE - fullBlocks * blockLen * 4; // assumed a multiple of 4
	const uint total = KDF_SIZE + extraBytes;
	// First, repeat the passed block an integral amount of times.
	event_t copied = 0;
	for(uint hash = 0; hash < get_local_size(1); hash++) {
		global uint *dst = (global uint*)(target + (get_group_id(1) * get_local_size(1) + hash) * total);
		for(uint block = 0; block < fullBlocks; block++) { // last uint shall be replaced by nonce.
			copied = async_work_group_copy(dst, pattern, blockLen, copied);
			dst += blockLen;
		}
		// The remaining bytes gets filled wrapping around. I could in fact just %
		// but I write it this way for self-documentation
		copied = async_work_group_copy(dst, pattern, remaining / 4, copied);
		dst += remaining / 4;
		// Extrabytes are basically the same.
		copied = async_work_group_copy(dst, pattern, extraBytes / 4, copied);
	}
	wait_group_events(1, &copied);
	// Now let's reset the various nonce values. It's just easier to do that using standard writes.
	// In theory I should do that even for remaining and extrabytes, but I don't.
	const uint nonce = (uint)(get_global_id(1));
	const uchar gidByte = nonce >> (8 * get_local_id(0));
	target += (get_global_id(1) - get_global_offset(1)) * total;
	for(uint out = 0; out < fullBlocks; out++) {
		const uint off = blockLen * 4 * out + (blockLen - 1) * 4;
		target[off + get_local_id(0)] = gidByte;
	}
}


void FillInitialBufferPrivate_4way(global uchar *dst, size_t extraBytes, uint *pattern, size_t blockLen) {
	// First, repeat the passed block an integral amount of times.
	const uint fullBlocks = KDF_SIZE / (blockLen * 4 * 4);
	for(uint block = 0; block < fullBlocks; block++) {
		for(uint cp = 0; cp < blockLen; cp++) {
			const uint value = pattern[cp];
			uint dsti = block * blockLen + cp * 4;
			for(uint byte = 0; byte < 4; byte++) dst[dsti * 4 + byte] = (uchar)(value >> (byte * 8));
		}
	}
	// The remaining bytes gets filled wrapping around. I could in fact just wrap-around this
	// but I write it this way for self-documentation.
	const uint remaining = KDF_SIZE - fullBlocks * blockLen * 4 * 4;
	for(uint cp = 0; cp < remaining / 4; cp++) { // assume it's a multiple of 4
		const uint value = pattern[cp];
		uint dsti = fullBlocks * blockLen + cp * 4;
		for(uint byte = 0; byte < 4; byte++) dst[dsti * 4 + byte] = (uchar)(value >> (byte * 8));
	}
	// Wrap-around again for the extra bytes. Assume fitting a block and 4n bytes.
	for(uint cp = 0; cp < extraBytes / 16; cp++) {
		const uint value = pattern[cp];
		uint dsti = KDF_SIZE / 4 + cp * 4;
		for(uint byte = 0; byte < 4; byte++) dst[dsti * 4 + byte] = (uchar)(value >> (byte * 8));
	}
}


__attribute__((reqd_work_group_size(4, 16, 1)))
kernel void firstKDF_4way(global uint *blockHeader, global uchar *output, const uint CONST_N, global uchar *buff_a, global uchar *buff_b) {
	const uint slot = get_global_id(1) - get_global_offset(1);
	/* There's a "very conveniently" called "A" buffer in legacy kernels.
	It is uchar[256+64] (FASTKDF_BUFFER_SIZE + BLAKE2S_BLOCK_SIZE).
	If you look at legacy kernels, this is basically read-only, filled and used in blocks.
	There's a quirk however: at each iteration, we need to pull up a "window" of those bytes.
	Registers cannot be arbitrarily indexed, therefore I should put this in LDS.
	With 4-way mangling this would be >4KiB of LDS for the buff_b alone. I'm not sure I can afford that.
	For now, I abuse global memory WHAT?
	Yes, global memory. We got plenty of arithmetic intensity anyway!
	There's also another "B" buffer, used for "keys". It is the same thing but quite smaller,
	it's uchar[256+32] (FASTKDF_BUFFER_SIZE + BLAKE2S_KEY_SIZE). They start being the same thing.

	We write "packed" values. That is, each consecutive value from a thread is get_global_size(0)
	bytes apart. Given address addr, addr+1 contains the same element but the value from WI (0)+1. */
	{
		local uint header[20];
		{
			event_t copied = async_work_group_copy(header, blockHeader, 20, 0);
			wait_group_events(1, &copied);
		}
		FillInitialBuffer(buff_a, 64, header);
		FillInitialBuffer(buff_b, 32, header);
		buff_a += slot * (KDF_SIZE + 64);
		buff_b += slot * (KDF_SIZE + 32);
	}
	local uint lds[16 * 33];
	// local uint *team = lds + get_local_id(1) * 33;
	uint buffStart = 0;
	for(uint loop = 0; loop < CONST_N; loop++) {
		barrier(CLK_GLOBAL_MEM_FENCE);
		buffStart = FastKDFIteration(lds, buffStart, buff_a, buff_b);
	}
	barrier(CLK_GLOBAL_MEM_FENCE);
	const uint outLen = 256;
	uint remaining = KDF_SIZE - buffStart;
	uint valid = min(remaining, outLen);
	// TODO: as buffStarts are all in LDS, I could have all WIs across different local-y
	// collaborate here, which would result in fully packed writes...
	// As I do this once, I'm not even sure it's worth it but considering the dst-strides
	// involved, I cannot exclude it either.
	const uint sliceStride = sizeof(uint);
	const uint hashGroup = slot / 64;
	const uint hashEntry = slot % 64;
	output += hashGroup * 64 * outLen + hashEntry * sliceStride;
	for(uint set = get_local_id(0); set < valid; set += get_local_size(0)) {
		uint dsti = (set / sliceStride) * 64 * sliceStride + set % sliceStride;
		output[dsti] = buff_b[set + buffStart] ^ buff_a[set];
	}
	for(uint set = valid + get_local_id(0); set < outLen; set += get_local_size(0)) {
		uint dsti = (set / sliceStride) * 64 * sliceStride + set % sliceStride;
		uint srci = set - valid;
		output[dsti] = buff_b[srci] ^ buff_a[srci + remaining];
	}
}


__attribute__((reqd_work_group_size(4, 16, 1)))
kernel void lastKDF_4way(volatile global uint *found, global uint *dispatchData,
                         global uint *stateo, global uint *statei, const uint CONST_N,
						 global uchar *buff_a, global uchar *buff_b, global uchar *output_to_test) {
	uint slot = get_global_id(1) - get_global_offset(1);
	{
		// Nice! buff_a was read only so it's ready to go for me :)
		buff_a += slot * (KDF_SIZE + 64);
		const uint stride = 4 * 64;
		stateo += (slot / 64) * 64 * 64 + get_local_id(0) * 64 + (slot % 64);
		statei += (slot / 64) * 64 * 64 + get_local_id(0) * 64 + (slot % 64);
		uint x[16];
		for(uint cp = 0; cp < 16; cp++) {
			x[cp]  = stateo[cp * stride];
			x[cp] ^= statei[cp * stride];
		}
		// Some serious shit there. Previously this was common to all WIs,
		// now it's hash-specific stuff and I have to write it strided, ouch!
		// For the first 256 bytes, just copy.
		// The remaining 32 bytes are wrapped-around.
		buff_b += slot * (KDF_SIZE + 32);
		FillInitialBufferPrivate_4way(buff_b + get_local_id(0) * 4, 32, x, 16);
	}
	local uint lds[16 * 33];
	uint buffStart = 0;
	for(uint loop = 0; loop < CONST_N; loop++) {
		barrier(CLK_GLOBAL_MEM_FENCE);
		buffStart = FastKDFIteration(lds, buffStart, buff_a, buff_b);
	}
	barrier(CLK_GLOBAL_MEM_FENCE);
	/* nonce selection here. Spill to memory. Ugly, but indexing registers scares me!
	... Why am I not doing this in LDS? Because of boredom and bank collisions. Both of which could be solved
	in some way but not right away. */
	const uint outLen = 32;
	output_to_test += outLen * slot;
	uint remaining = KDF_SIZE - buffStart;
	uint valid = min(remaining, outLen);
	for(uint set = get_local_id(0); set < valid; set += get_local_size(0)) {
		output_to_test[set] = buff_b[set + buffStart] ^ buff_a[set];
	}
	for(uint set = valid + get_local_id(0); set < outLen; set += get_local_size(0)) {
		uint srci = set - valid;
		output_to_test[set] = buff_b[srci] ^ buff_a[srci + remaining];
	}
	barrier(CLK_GLOBAL_MEM_FENCE);
	if(get_local_id(0) == 0) {
		global uint *finalHash = (global uint*)output_to_test;
        const ulong magic = upsample(finalHash[7], finalHash[6]);
        const ulong target =  upsample(dispatchData[1], dispatchData[2]); // watch out for endianess!
		lds[get_local_id(1)] = magic < target;
        if(magic < target) {
			uint storage = atomic_inc(found);
			uint nonce = (uint)(get_global_id(1));
			found[1 + storage * (outLen / 4 + 1)] = as_uint(as_uchar4(nonce).wzyx);
			lds[get_local_size(1) + get_local_id(1)] = storage;
		}
	}
	barrier(CLK_LOCAL_MEM_FENCE);
	uint candidate = 0;
	for(uint slot = 0; slot < get_local_size(1); slot++) candidate = slot == get_local_id(1)? lds[slot] : candidate;
	if(candidate) { // taken from echo8W but not very efficient, would be much better to pack writes, but that rarely happens anyway!
	    found++;
        candidate = lds[get_local_size(1) + get_local_id(1)]; // very **likely** broadcast
		found += candidate * 9;
        found++; // this one is the nonce, already stored
		global uchar *hashOut = (global uchar*)(found);
		for(uint set = get_local_id(0); set < outLen; set += get_local_size(0)) hashOut[set] = output_to_test[set];
	}
}
