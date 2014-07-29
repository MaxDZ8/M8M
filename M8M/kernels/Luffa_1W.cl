/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
/* Luffa-512.
That is, 64 bytes, or 16 uints, the typical size of a BTC protocol hash.

Luffa-512 does not use so many registers. In initial tests, this used 97 VGPRs.
Occupancy is only 20% but this still gives no more than 6% memory stalls.
Parallelizing it would be to make it more efficient memory wise. Unfortunately,
I haven't quite understood parallel luffa.
 
This code produces a slightly smaller executable (~2%). This is slightly above 17k
instruction bytes thereby slightly exceeding the cache. It ends up being around 2%
faster as well. Odds are parallelizing (perhaps 2-way) would give me a good
advantage due to a better cache fit but...

Luffa is hard. State is 8x5 (five), uses polynomial multiplication which has slightly
divergent threading, it seems to have quite some parallelism but at closer scrutinity
you figure out some parallelism is "by column" while some other is "by row". SubCrumb sucks
and there's not enough room to mess up with LDS layouts as legacy implementations are still
quite efficient.

No overspilling no fun, this hash is not much of an improvement.
Enjoy the cleaner code, MaxDZ8. */

// Page 15 of the specification: round constants are generated sequentially from fixed initial values
// and applying the pseudocode provided in the same section. At page 13 we see those constants are
// are XORed to component 0 and 4 of each 8-word.
constant static uint2 roundConstant[5][8] = {
	{
		(uint2)(0x303994A6u, 0xE0337818u), (uint2)(0xC0E65299u, 0x441BA90Du), (uint2)(0x6CC33A12u, 0x7F34D442u), (uint2)(0xDC56983Eu, 0x9389217Fu),
		(uint2)(0x1E00108Fu, 0xE5A8BCE6u), (uint2)(0x7800423Du, 0x5274BAF4u), (uint2)(0x8F5B7882u, 0x26889BA7u), (uint2)(0x96E1DB12u, 0x9A226E9Du)
	},
	{
		(uint2)(0xB6DE10EDu, 0x01685F3Du), (uint2)(0x70F47AAEu, 0x05A17CF4u), (uint2)(0x0707A3D4u, 0xBD09CACAu), (uint2)(0x1C1E8F51u, 0xF4272B28u),
		(uint2)(0x707A3D45u, 0x144AE5CCu), (uint2)(0xAEB28562u, 0xFAA7AE2Bu), (uint2)(0xBACA1589u, 0x2E48F1C1u), (uint2)(0x40A46F3Eu, 0xB923C704u)
	},
	{
		(uint2)(0xFC20D9D2u, 0xE25E72C1u), (uint2)(0x34552E25u, 0xE623BB72u), (uint2)(0x7AD8818Fu, 0x5C58A4A4u), (uint2)(0x8438764Au, 0x1E38E2E7u),
		(uint2)(0xBB6DE032u, 0x78E38B9Du), (uint2)(0xEDB780C8u, 0x27586719u), (uint2)(0xD9847356u, 0x36EDA57Fu), (uint2)(0xA2C78434u, 0x703AACE7u)
	},
	{
		(uint2)(0xB213AFA5u, 0xE028C9BFu), (uint2)(0xC84EBE95u, 0x44756F91u), (uint2)(0x4E608A22u, 0x7E8FCE32u), (uint2)(0x56D858FEu, 0x956548BEu),
		(uint2)(0x343B138Fu, 0xFE191BE2u), (uint2)(0xD0EC4E3Du, 0x3CB226E5u), (uint2)(0x2CEB4882u, 0x5944A28Eu), (uint2)(0xB3AD2208u, 0xA1C4C355u)
	},
	{
		(uint2)(0xF0D2E9E3u, 0x5090D577u), (uint2)(0xAC11D7FAu, 0x2D1925ABu), (uint2)(0x1BCB66F2u, 0xB46496ACu), (uint2)(0x6F2D9BC9u, 0xD1925AB0u),
		(uint2)(0x78602649u, 0x29131AB6u), (uint2)(0x8EDAE952u, 0x0FC053C3u), (uint2)(0x3B6BA548u, 0x3F014F0Cu), (uint2)(0xEDAE9520u, 0xFC053C31u)
	}
};


uint8 MulTwo(uint8 a) {
	return (uint8)(a.s7,         a.s0 ^ a.s7, a.s1, a.s2 ^ a.s7,
	               a.s3 ^ a.s7, a.s4,         a.s5, a.s6);
}


void Tweak(uint8 *V, uint i) {
	V[i].hi = rotate(V[i].hi, i);
}


uint4 SubCrumb(uint4 v) {
	uint temp = v.s0;
	v.s0 |= v.s1;
	v.s2 ^= v.s3;
	v.s1 = as_uint(~v.s1);
	v.s0 ^= v.s3;
	v.s3 &= temp;
	v.s1 ^= v.s3;
	v.s3 ^= v.s2;
	v.s2 &= v.s0;
	v.s0 = as_uint(~v.s0);
	v.s2 ^= v.s1;
	v.s1 |= v.s3;
	temp ^= v.s1;
	v.s3 ^= v.s2;
	v.s2 &= v.s1;
	v.s1 ^= v.s0;
	v.s0 = temp;
	return v;
}


uint2 MixWord(uint2 v) {
	v.y ^= v.x;
	v.x = rotate(v.x, 2u) ^ v.y;
	v.y = rotate(v.y, 14u) ^ v.x;
	v.x = rotate(v.x, 10u) ^ v.y;
	v.y = rotate(v.y, 1u);
	return v;
}

kernel void Luffa_1way(global uint *wuData, global uint *hashOut) {
	uint8 V[5] = {
		(uint8)(0x6D251E69u, 0x44B051E0u, 0x4EAA6FB4u, 0xDBF78465u, 0x6E292011u, 0x90152DF4u, 0xEE058139u, 0xDEF610BBu),
		(uint8)(0xC3B44B95u, 0xD9D2F256u, 0x70EEE9A0u, 0xDE099FA3u, 0x5D9B0557u, 0x8FC944B3u, 0xCF1CCF0Eu, 0x746CD581u),
		(uint8)(0xF7EFC89Du, 0x5DBA5781u, 0x04016CE5u, 0xAD659C05u, 0x0306194Fu, 0x666D1836u, 0x24AA230Au, 0x8B264AE7u),
		(uint8)(0x858075D5u, 0x36D79CCEu, 0xE571F7D7u, 0x204B1F67u, 0x35870C6Au, 0x57E9E923u, 0x14BCB808u, 0x7CDE72CEu),
		(uint8)(0x6C68E9BEu, 0x5EC41E22u, 0xC825B7C7u, 0xAFFB4363u, 0xF5DF3999u, 0x0FC688F1u, 0xB07224CCu, 0x03E86CEAu)
	};

#if !defined(LUFFA_HEAD)
#error To be adapted for higher degree chained hashing.
#endif
	hashOut += (get_global_id(0) - get_global_offset(0)) * 16;

	uint8 M = (uint8)(wuData[0], wuData[1], wuData[2], wuData[3],
	                  wuData[4], wuData[5], wuData[6], wuData[7]);
    for(uint i = 0; i < 5; i++)
    {
		/* Message Injection function MI for w=5, luffa specification pag 26.
		If you take the specification and read the image "by column" you see this
		can be thought as 4 steps:
		1) From input to first parallel XOR
		2) First "up" feistel to second parallel XOR
		3) Second "down" feistel to third parallel XOR
		4) XORring with (multiplied) M */
		{
			uint8 initial = MulTwo(V[0] ^ V[1] ^ V[2] ^ V[3] ^ V[4]);
			V[0] ^= initial;
			V[1] ^= initial;
			V[2] ^= initial;
			V[3] ^= initial;
			V[4] ^= initial;
		}
		{
			uint8 temp = V[0];
			V[0] = MulTwo(V[0]) ^ V[1];
			V[1] = MulTwo(V[1]) ^ V[2];
			V[2] = MulTwo(V[2]) ^ V[3];
			V[3] = MulTwo(V[3]) ^ V[4];
			V[4] = MulTwo(V[4]) ^ temp;
		}
		{
			uint8 temp = V[4];
			V[4] = MulTwo(V[4]) ^ V[3];
			V[3] = MulTwo(V[3]) ^ V[2];
			V[2] = MulTwo(V[2]) ^ V[1];
			V[1] = MulTwo(V[1]) ^ V[0];
			V[0] = MulTwo(V[0]) ^ temp;
		}
		{
			V[0] ^= M;    M = MulTwo(M);
			V[1] ^= M;    M = MulTwo(M);
			V[2] ^= M;    M = MulTwo(M);
			V[3] ^= M;    M = MulTwo(M);
			V[4] ^= M;
		}
		// Now the permutations. Those are 5 functions Qi, see page 13 for pseudocode.
		Tweak(V, 1u);
		Tweak(V, 2u);
		Tweak(V, 3u);
		Tweak(V, 4u);
		for(uint reg = 0; reg < 5; reg++) {
			for(uint p = 0; p < 8; p++) {
				V[reg].lo    = SubCrumb(V[reg].lo);
				V[reg].s5674 = SubCrumb(V[reg].s5674);
				V[reg].s04 = MixWord(V[reg].s04);
				V[reg].s15 = MixWord(V[reg].s15);
				V[reg].s26 = MixWord(V[reg].s26);
				V[reg].s37 = MixWord(V[reg].s37);
				V[reg].s04 ^= roundConstant[reg][p];
			}
		}

        if(i == 0) {
			M = (uint8)(wuData[ 8], wuData[ 9], wuData[10], wuData[11],
			            wuData[12], wuData[13], wuData[14], wuData[15]);
        } else if(i == 1) {
			M = (uint8)(wuData[16], wuData[17], wuData[18], as_uint(as_uchar4(get_global_id(0)).wzyx),
			            0x80000000u, 0, 0, 0);
        } else if(i == 2) {
			M = (uint8)(0);
        } else if(i == 3) {
            hashOut[1] = V[0].s0 ^ V[1].s0 ^ V[2].s0 ^ V[3].s0 ^ V[4].s0;
            hashOut[0] = V[0].s1 ^ V[1].s1 ^ V[2].s1 ^ V[3].s1 ^ V[4].s1;
            hashOut[3] = V[0].s2 ^ V[1].s2 ^ V[2].s2 ^ V[3].s2 ^ V[4].s2;
            hashOut[2] = V[0].s3 ^ V[1].s3 ^ V[2].s3 ^ V[3].s3 ^ V[4].s3;
            hashOut[5] = V[0].s4 ^ V[1].s4 ^ V[2].s4 ^ V[3].s4 ^ V[4].s4;
            hashOut[4] = V[0].s5 ^ V[1].s5 ^ V[2].s5 ^ V[3].s5 ^ V[4].s5;
            hashOut[7] = V[0].s6 ^ V[1].s6 ^ V[2].s6 ^ V[3].s6 ^ V[4].s6;
            hashOut[6] = V[0].s7 ^ V[1].s7 ^ V[2].s7 ^ V[3].s7 ^ V[4].s7;
        }
    }
    hashOut[ 9] = V[0].s0 ^ V[1].s0 ^ V[2].s0 ^ V[3].s0 ^ V[4].s0;
    hashOut[ 8] = V[0].s1 ^ V[1].s1 ^ V[2].s1 ^ V[3].s1 ^ V[4].s1;
    hashOut[11] = V[0].s2 ^ V[1].s2 ^ V[2].s2 ^ V[3].s2 ^ V[4].s2;
    hashOut[10] = V[0].s3 ^ V[1].s3 ^ V[2].s3 ^ V[3].s3 ^ V[4].s3;
    hashOut[13] = V[0].s4 ^ V[1].s4 ^ V[2].s4 ^ V[3].s4 ^ V[4].s4;
    hashOut[12] = V[0].s5 ^ V[1].s5 ^ V[2].s5 ^ V[3].s5 ^ V[4].s5;
    hashOut[15] = V[0].s6 ^ V[1].s6 ^ V[2].s6 ^ V[3].s6 ^ V[4].s6;
    hashOut[14] = V[0].s7 ^ V[1].s7 ^ V[2].s7 ^ V[3].s7 ^ V[4].s7;
}
