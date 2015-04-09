#pragma once
#include "../BlockVerifierInterface.h"

extern "C" {
#include "../../SPH/sph_luffa.h"
#include "../../SPH/sph_cubehash.h"
#include "../../SPH/sph_shavite.h"
#include "../../SPH/sph_simd.h"
#include "../../SPH/sph_echo.h"
};


namespace bv {


class Qubit : public BlockVerifierInterface {
public:
	std::array<aubyte, 32> Hash(std::array<aubyte, 80> baseBlockHeader, auint nonce) {
        nonce = HTON(nonce);
        memcpy_s(baseBlockHeader.data() + 76, sizeof(baseBlockHeader) - 76, &nonce, sizeof(nonce));
		aubyte one[64], two[64];
		{
			sph_luffa512_context head;
			sph_luffa512_init(&head);
			sph_luffa512(&head, baseBlockHeader.data(), sizeof(baseBlockHeader));
			sph_luffa512_close(&head, one);
		}
		{
			sph_cubehash512_context ctx;
			sph_cubehash512_init(&ctx);
			sph_cubehash512(&ctx, one, sizeof(one));
			sph_cubehash512_close(&ctx, two);
		}
		{
			sph_shavite512_context ctx;
			sph_shavite512_init(&ctx);
			sph_shavite512(&ctx, two, sizeof(two));
			sph_shavite512_close(&ctx, one);
		}
		{
			sph_simd512_context ctx;
			sph_simd512_init(&ctx);
			sph_simd512(&ctx, one, sizeof(one));
			sph_simd512_close(&ctx, two);
		}
		{
			sph_echo512_context ctx;
			sph_echo512_init(&ctx);
			sph_echo512(&ctx, two, sizeof(two));
			sph_echo512_close(&ctx, one);
		}
		std::array<aubyte, 32> hash;
		memcpy_s(hash.data(), sizeof(hash), one, 32);
		return hash;
	}
};


}
