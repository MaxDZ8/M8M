/*
 * Copyright (C) 2014 Massimo Del Zotto
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
uint LE_UINT_LOAD(uint v) {
#if __ENDIAN_LITTLE__
	return as_uint(as_uchar4(v).wzyx);
#else 
#error memory load needs care.
#endif
}


constant uint initialState[16][2] = {
	{ 0x2AEA2A61u, 0x50F494D4u },
	{ 0x2D538B8Bu, 0x4167D83Eu },
	{ 0x3FEE2313u, 0xC701CF8Cu },
	{ 0xCC39968Eu, 0x50AC5695u },
	{ 0x4D42C787u, 0xA647A8B3u },
	{ 0x97CF0BEFu, 0x825B4537u },
	{ 0xEEF864D2u, 0xF22090C4u },
	{ 0xD0E5CD33u, 0xA23911AEu },
	{ 0xFCD398D9u, 0x148FE485u },
	{ 0x1B017BEFu, 0xB6444532u },
	{ 0x6A536159u, 0x2FF5781Cu },
	{ 0x91FA7934u, 0x0DBADEA9u },
	{ 0xD65C8A2Bu, 0xA5A70E75u },
	{ 0xB1C62456u, 0xBC796576u },
	{ 0x1921C8F7u, 0xE7989AF1u },
	{ 0x7795D246u, 0xD43E3B44u }
};


void CubeHash_2W_EvnRound(uint *lo, local uint *hi) {
	hi[0 * 32] += lo[0];
	hi[1 * 32] += lo[1];
	hi[2 * 32] += lo[2];
	hi[3 * 32] += lo[3];
	hi[4 * 32] += lo[4];
	hi[5 * 32] += lo[5];
	hi[6 * 32] += lo[6];
	hi[7 * 32] += lo[7];
	lo[0] = rotate(lo[0], 7u);
	lo[1] = rotate(lo[1], 7u);
	lo[2] = rotate(lo[2], 7u);
	lo[3] = rotate(lo[3], 7u);
	lo[4] = rotate(lo[4], 7u);
	lo[5] = rotate(lo[5], 7u);
	lo[6] = rotate(lo[6], 7u);
	lo[7] = rotate(lo[7], 7u);
	lo[4] ^= hi[(0 + 0) * 32];
	lo[5] ^= hi[(1 + 0) * 32];
	lo[6] ^= hi[(2 + 0) * 32];
	lo[7] ^= hi[(3 + 0) * 32];
	lo[0] ^= hi[(0 + 4) * 32];
	lo[1] ^= hi[(1 + 4) * 32];
	lo[2] ^= hi[(2 + 4) * 32];
	lo[3] ^= hi[(3 + 4) * 32];
	hi[1 * 32] += lo[4];
	hi[0 * 32] += lo[5];
	hi[3 * 32] += lo[6];
	hi[2 * 32] += lo[7];
	hi[5 * 32] += lo[0];
	hi[4 * 32] += lo[1];
	hi[7 * 32] += lo[2];
	hi[6 * 32] += lo[3];
	lo[0] = rotate(lo[0], 11u);
	lo[1] = rotate(lo[1], 11u);
	lo[2] = rotate(lo[2], 11u);
	lo[3] = rotate(lo[3], 11u);
	lo[4] = rotate(lo[4], 11u);
	lo[5] = rotate(lo[5], 11u);
	lo[6] = rotate(lo[6], 11u);
	lo[7] = rotate(lo[7], 11u);
	lo[0] ^= hi[7 * 32];
	lo[1] ^= hi[6 * 32];
	lo[2] ^= hi[5 * 32];
	lo[3] ^= hi[4 * 32];
	lo[4] ^= hi[3 * 32];
	lo[5] ^= hi[2 * 32];
	lo[6] ^= hi[1 * 32];
	lo[7] ^= hi[0 * 32];
}


void CubeHash_2W_OddRound(uint *lo, local uint *hi) {
	// The odd round is a bit more complicated in the 2-way CubeHash.
	// Main problem is: WI0 handles "even" value index. WI1 handles "odd" value index.
	// BUT in line of theory we should have swapped the values here. The private values
	// must indeed stay where they are. I just swap the pointer.
	// In the two-way formulation, swapping LDS columns is dead simple given current layout!
	hi = hi + (get_local_id(0) == 0? 1 : -1);
	// from now on, hi[0*32] is x16 for WI1
	
	hi[1 * 32] += lo[6];
	hi[0 * 32] += lo[7];
	hi[3 * 32] += lo[4];
	hi[2 * 32] += lo[5];
	hi[5 * 32] += lo[2];
	hi[4 * 32] += lo[3];
	hi[7 * 32] += lo[0];
	hi[6 * 32] += lo[1];
	lo[0] = rotate(lo[0], 7u);
	lo[1] = rotate(lo[1], 7u);
	lo[2] = rotate(lo[2], 7u);
	lo[3] = rotate(lo[3], 7u);
	lo[4] = rotate(lo[4], 7u);
	lo[5] = rotate(lo[5], 7u);
	lo[6] = rotate(lo[6], 7u);
	lo[7] = rotate(lo[7], 7u);	
	lo[0] ^= hi[(0 + 3) * 32];
	lo[1] ^= hi[(0 + 2) * 32];
	lo[2] ^= hi[(0 + 1) * 32];
	lo[3] ^= hi[(0 + 0) * 32];
	lo[4] ^= hi[(4 + 3) * 32];
	lo[5] ^= hi[(4 + 2) * 32];
	lo[6] ^= hi[(4 + 1) * 32];
	lo[7] ^= hi[(4 + 0) * 32];
	hi[0 * 32] += lo[2];
	hi[1 * 32] += lo[3];
	hi[2 * 32] += lo[0];
	hi[3 * 32] += lo[1];
	hi[4 * 32] += lo[6];
	hi[5 * 32] += lo[7];
	hi[6 * 32] += lo[4];
	hi[7 * 32] += lo[5];
	lo[0] = rotate(lo[0], 11u);
	lo[1] = rotate(lo[1], 11u);
	lo[2] = rotate(lo[2], 11u);
	lo[3] = rotate(lo[3], 11u);
	lo[4] = rotate(lo[4], 11u);
	lo[5] = rotate(lo[5], 11u);
	lo[6] = rotate(lo[6], 11u);
	lo[7] = rotate(lo[7], 11u);
	lo[0] ^= hi[0 * 32];
	lo[1] ^= hi[1 * 32];
	lo[2] ^= hi[2 * 32];
	lo[3] ^= hi[3 * 32];
	lo[4] ^= hi[4 * 32];
	lo[5] ^= hi[5 * 32];
	lo[6] ^= hi[6 * 32];
	lo[7] ^= hi[7 * 32];
}


void CubeHash_2W_Pass(uint *lo, local uint *hi) {
	for(int j = 0; j < 8; j++) {
		CubeHash_2W_EvnRound(lo, hi);
		CubeHash_2W_OddRound(lo, hi);
	}
}


__attribute__((reqd_work_group_size(2, 32, 1)))
kernel void CubeHash_2way(global uint *input, global uint *hashOut) {
	/* Two-way CubeHash is this way: even registers go in local work unit x-0 while odd registers go in x-1.
	BUT only the lower 0..15 values are in regs. Others are in LDS. We therefore get much better occupancy, allowing
	the memory unit to not stall. Hopefully. */
	uint lo[8];
	for(uint i = 0; i < 8; i++) lo[i] = initialState[i][get_local_id(0)];
	local uint lds[8 * 2 * 32];
	local uint *hi = lds + get_local_id(0) + (get_local_id(1) % 16) * 2;
	hi += get_local_id(1) >= 16? 8 * 2 * 16 : 0;
	for(uint i = 0; i < 8; i++) hi[i * 32] = initialState[8 + i][get_local_id(0)];
	input   += (get_global_id(1) - get_global_offset(1)) * 16;
	hashOut += (get_global_id(1) - get_global_offset(1)) * 16 + get_local_id(0);
	
	lo[0] ^= LE_UINT_LOAD(input[1 - get_local_id(0)]);
	lo[1] ^= LE_UINT_LOAD(input[3 - get_local_id(0)]);
	lo[2] ^= LE_UINT_LOAD(input[5 - get_local_id(0)]);
	lo[3] ^= LE_UINT_LOAD(input[7 - get_local_id(0)]);
	
	for(uint pass = 0; pass < 13; pass++) {
		CubeHash_2W_Pass(lo, hi);
		switch(pass) {
		case 0:
			lo[0] ^= LE_UINT_LOAD(input[ 9 - get_local_id(0)]);
			lo[1] ^= LE_UINT_LOAD(input[11 - get_local_id(0)]);
			lo[2] ^= LE_UINT_LOAD(input[13 - get_local_id(0)]);
			lo[3] ^= LE_UINT_LOAD(input[15 - get_local_id(0)]);
			break;
		case 1:
			if(get_local_id(0) == 0) lo[0] ^= 0x00000080;
			break;
		case 2:
			if(get_local_id(0) == 1) hi[7 * 32] ^= 0x00000001;
			break;
		}
	}
	
	hashOut[2 * 0] = lo[0];
	hashOut[2 * 1] = lo[1];
	hashOut[2 * 2] = lo[2];
	hashOut[2 * 3] = lo[3];
	hashOut[2 * 4] = lo[4];
	hashOut[2 * 5] = lo[5];
	hashOut[2 * 6] = lo[6];
	hashOut[2 * 7] = lo[7];
}
