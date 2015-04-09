#pragma once
#include "../BlockVerifierInterface.h"
#include <memory>

#include <fstream>

namespace bv {


class GenericNeoScrypt : public BlockVerifierInterface {
	static const auint blake2S_IV[8];
	static const aubyte blake2S_sigma[10][16];

    const auint kdfSize;
    const auint kdfConstN;
    const auint mixRounds;
    const auint iterations;

protected:
    GenericNeoScrypt(auint KDF_SIZE, auint KDF_CONST_N, auint MIX_ROUNDS, auint ITERATIONS)
        : kdfSize(KDF_SIZE), kdfConstN(KDF_CONST_N), mixRounds(MIX_ROUNDS), iterations(ITERATIONS) { }

	// The following four functions are taken from the CL code directly for easiness.
	void Salsa(auint state[16]);
	void Chacha(auint state[16]);
	void FillInitialBuffer(aubyte *target, auint extraBytes, const aubyte *pattern, auint patternCountUint);
	static void Blake2S_64_32(auint *output, auint *input, auint *key, const auint numRounds);
	static std::array<auint, 8> Blake2SBlockXForm(const std::array<auint, 8> hash, const std::array<auint, 4> &counter, const auint numRounds, const std::array<auint, 16> &msg);
	std::array<auint, 64> FirstKDF(const aubyte *block, aubyte *buff_a, aubyte *buff_b);
	std::array<aubyte, 32> LastKDF(const std::array<auint, 64> &state, const aubyte *buff_a, aubyte *buff_b);
	auint FastKDFIteration(auint buffStart, const aubyte *buff_a, aubyte *buff_b);
};


//! We had it easy so far. Now stuff gets real. This is a template helper so I can build templated stuff using this with ease!
template<auint KDF_SIZE, auint KDF_CONST_N, auint MIX_ROUNDS, auint ITERATIONS>
class NeoScrypt : public GenericNeoScrypt {
public:
    explicit NeoScrypt() : GenericNeoScrypt(KDF_SIZE, KDF_CONST_N, MIX_ROUNDS, ITERATIONS) { }
	std::array<aubyte, 32> Hash(std::array<aubyte, 80> baseBlockHeader, auint nonce) {
	    memcpy_s(baseBlockHeader.data() + 76, sizeof(baseBlockHeader) - 76, &nonce, sizeof(nonce));
	    aubyte buff_a[256 + 64], buff_b[256 + 32];
	    std::array<aubyte, 80> endianess;
	    for(auint i = 0; i < sizeof(baseBlockHeader); i += 4) {
		    for(auint b = 0; b < 4; b++) endianess[i + b] = baseBlockHeader[i + 3 - b];
	    }
	    auto initial(FirstKDF(endianess.data(), buff_a, buff_b));
	    if(!pad) pad.reset(new auint[ITERATIONS * 64]);
	    auto work(initial);
	    auto salsa = [this](auint state[16]) { Salsa(state); }; // that's a bit backwards but I don't like alternatives either.
	    auto chacha = [this](auint state[16]) { Chacha(state); };
	
	    SequentialWrite(pad.get(), work.data(), salsa);
	    IndirectedRead(work.data(), pad.get(), salsa);
	    SequentialWrite(pad.get(), initial.data(), chacha);
	    IndirectedRead(initial.data(), pad.get(), chacha);

	    for(auint el = 0; el < initial.size(); el++) work[el] ^= initial[el];
	    return LastKDF(work, buff_a, buff_b);
    }

private:
	std::unique_ptr<auint[]> pad;

	// As checking isn't considered a performance path I could avoid using a template here: they are still a bit ugly to debuggers and messages.
	template<typename MixFunc>
	void SequentialWrite(auint *pad, auint *state, MixFunc &&mix) {
		static const auint perm[2][4] = {
			{0, 1, 2, 3},
			{0, 2, 1, 3}
		};
		for(auint loop = 0; loop < ITERATIONS; loop++) {
			for(auint slice = 0; slice < 4; slice++) {
				auint *one = state + perm[loop % 2][slice] * 16;
				auint *two = state + perm[loop % 2][(slice + 3) % 4] * 16;
				auint prev[16];
				for(auint el = 0; el < 16; el++) {
					pad[el] = one[el];
					one[el] ^= two[el];
					prev[el] = one[el];
				}
				pad += 16;
				mix(one);
				for(auint el = 0; el < 16; el++) one[el] += prev[el];
			}
		}
	}
	template<typename MixFunc>
	void IndirectedRead(auint *state, auint *pad, MixFunc &&mix) {
		static const auint perm[2][4] = {
			{0, 1, 2, 3},
			{0, 2, 1, 3}
		};
		for(auint loop = 0; loop < ITERATIONS; loop++) {
			const auint indirected = state[48] % 128;
			for(auint slice = 0; slice < 4; slice++) {
				auint *one = state + perm[loop % 2][slice] * 16;
				for(auint el = 0; el < 16; el++) one[el] ^= pad[indirected * 64 + slice * 16 + el];
			}
			for(auint slice = 0; slice < 4; slice++) {
				auint *one = state + perm[loop % 2][slice] * 16;
				auint *two = state + perm[loop % 2][(slice + 3) % 4] * 16;
				auint prev[16];
				for(auint el = 0; el < 16; el++) {
					one[el] ^= two[el];
					prev[el] = one[el];
				}
				mix(one);
				for(auint el = 0; el < 16; el++) one[el] += prev[el];
			}
		}
	}
};


}
