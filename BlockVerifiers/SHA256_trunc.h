/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "HashBlocks.h"

/*! A SHA256 missing a few last steps. Used by 
- myriadcoin-groestl, which is sha256_trunc(groestl_512(h)).

Some additional documentation from the original myr-grs verifier follows.

What's going on here?
Legacy miners just do a SHA256 here, which is precisely what you expect: hash = SHA256(GROESTL512(header_nonced))
The point is that if you look at the OpenCL kernel, it's not computing the hash as above.
It rather computes SHA256_TRUNCATED(GROESTL512(header_nonced)).
Where is it truncated? The first 64-byte block is computed as usual, then the remaining bytes + padding (always the same)
can be partially precomputed... sphlib sha2.c:569 does W12=... it is the first thing NOT executed.
Legacy kernels use a macro called PLAST to signal this. SPHLib does it fully and thus obviously the results cannot converge.
SPHlib sha2.c:593 is the place where we can think at the computation converging again. Basically the truncated sha hash gets added to the previous.

So basically we skip a whole ABCD EFGH update. 
Now the big question is: if the two functions are different, how exactly legacy miners can validate with SHA256(GROESTL(h))?
To be better investigated. */
struct SHA256_trunc : IntermediateHasherInterface {
    std::vector<aubyte>& Hash(std::vector<aubyte> &hash, const std::vector<aubyte> &input) {
        std::array<auint, 16> temp;
        memcpy_s(temp.data(), sizeof(temp), input.data(), sizeof(input[0]) * input.size());
        SHA256(temp.data());
        hash.resize(8 * sizeof(auint));
        for(auint i = 0; i < 8; i++) temp[i] = SWAP_BYTES(temp[i]);
        memcpy_s(hash.data(), 32, temp.data(), 32);
        return hash;
    }
    bool CanMangle(asizei inputByteCount) const { return inputByteCount == 16 * sizeof(auint); }
    asizei GetHashByteCount() const { return 8 * sizeof(auint); }

private:
    auint SwapUintBytes(auint val) {  //! \todo take care of endianess!
        aubyte *b = reinterpret_cast<aubyte*>(&val);
        aubyte bytes[4];
        for(auint cp = 0; cp < 4; cp++) bytes[cp] = b[3 - cp];
        for(auint cp = 0; cp < 4; cp++) b[cp] = bytes[cp];
        return val;
    }

    //! Matching CL 1.2
    auint bitselect(auint a, auint b, auint c) {
        auint res = 0;
        for(auint bit = 0; bit < 32; bit++) {
            const auint mask = 1 << bit;
            res |= (c & mask) == 0? (a & mask) : (b & mask);
        }
        return res;
    }

    auint ROL32(auint x, auint n) { return _rotl(x, n); }
    auint SHR(auint x, auint n) { return x >> n; }
    auint F0(auint y, auint x, auint z) { return bitselect(z, y, z ^ x); }
    auint F1(auint x, auint y, auint z) { return bitselect(z, y, x); }
    auint S0(auint x) { return ROL32(x, 25u) ^ ROL32(x, 14u) ^ SHR(x, 3u); }
    auint S1(auint x) { return ROL32(x, 15u) ^ ROL32(x, 13u) ^ SHR(x, 10u); }
    auint S2(auint x) { return ROL32(x, 30u) ^ ROL32(x, 19u) ^ ROL32(x, 10u); }
    auint S3(auint x) { return ROL32(x, 26u) ^ ROL32(x, 21u) ^ ROL32(x, 7u); }

    
    /*! SHA is a combination of various slightly similar rounds.
    As a matter of fact, it's better to think at those as "a standard round preceded by
    some operation". This is the basic round.
    Copied from CL implementation. */
    void SHARound_Set(auint *v, auint *w, const auint *k) {
        auint vals[8];
        for(auint cp = 0; cp < 8; cp++) vals[cp] = v[cp];
        for(auint i = 0; i < 16; i++) {
            auint temp = vals[7] + k[i] + w[i];
            temp += S3(vals[4]) + F1(vals[4], vals[5], vals[6]);
            vals[3] += temp;
            vals[7] = temp + S2(vals[0]) + F0(vals[0], vals[1], vals[2]);
        
            const auint seven = vals[7];
            for(auint cp = 7; cp; cp--) vals[cp] = vals[cp - 1];
            vals[0] = seven; // hopefully the compiler unrolls this for me
        }
        for(auint cp = 0; cp < 8; cp++) v[cp] = vals[cp];
    }
    /*! In "update" rounds W values are updated before being used.
    In legacy kernels this looks very similar as they use Rx values instead of W,
    where Rx values are macros expanding to the update pass.
    Copied from CL implementation. */
    void SHARound_Update(auint *v, auint *w, const auint *k) {
        aint vals[8];
        for(auint cp = 0; cp < 8; cp++) vals[cp] = v[cp];
        for(auint i = 0; i < 16; i++) {
            w[i] += S1(w[(i + 14) % 16]) + w[(i + 9) % 16] + S0(w[(i + 1) % 16]); // <--
            auint temp = vals[7] + k[i] + w[i];
            temp += S3(vals[4]) + F1(vals[4], vals[5], vals[6]);
            vals[3] += temp;
            vals[7] = temp + S2(vals[0]) + F0(vals[0], vals[1], vals[2]);

            const auint seven = vals[7];
            for(auint cp = 7; cp; cp--) vals[cp] = vals[cp - 1];
            vals[0] = seven; // hopefully the compiler unrolls this for me
        }
        for(auint cp = 0; cp < 8; cp++) v[cp] = vals[cp];
    }
    
    /*! In legacy kernels the last steps don't even use Rx macros but rather RDx.
    For us, that's a bit more complicated: for readability reasons we use functions BUT
    in case functions get NOT inlined (not default, but sometimes happens) I cannot just
    branch on some parameter or I'd get some slowdown.
    Usually this does not happen but anyway, let's stress the differences. */
    void SHARound_Update_Last(auint *v, auint *w, const auint *k) {
        aint vals[8];
        for(auint cp = 0; cp < 8; cp++) vals[cp] = v[cp];
        for(auint i = 0; i < 14; i++) {
            w[i] += S1(w[(i + 14) % 16]) + w[(i + 9) % 16] + S0(w[(i + 1) % 16]);
            auint temp = vals[7] + k[i] + w[i];
            temp += S3(vals[4]) + F1(vals[4], vals[5], vals[6]);
            vals[3] += temp;
            vals[7] = temp + S2(vals[0]) + F0(vals[0], vals[1], vals[2]);

            const auint seven = vals[7];
            for(auint cp = 7; cp; cp--) vals[cp] = vals[cp - 1];
            vals[0] = seven; // hopefully the compiler unrolls this for me
        }
        for(auint i = 14; i < 16; i++) {
            auint last = w[i] + S1(w[(i + 14) % 16]) + w[(i + 9) % 16] + S0(w[(i + 1) % 16]);
            auint temp = vals[7] + k[i] + last; // a completely new "w[i]" value
            temp += S3(vals[4]) + F1(vals[4], vals[5], vals[6]);
            vals[3] += temp;
            vals[7] = temp + S2(vals[0]) + F0(vals[0], vals[1], vals[2]);

            const auint seven = vals[7];
            for(auint cp = 7; cp; cp--) vals[cp] = vals[cp - 1];
            vals[0] = seven; // hopefully the compiler unrolls this for me
        }
        for(auint cp = 0; cp < 8; cp++) v[cp] = vals[cp];
    }

    
    /*! Last but not least, there's another round variation where we use known
    constants instead of W values. */
    void SHAHalfRound_Constant(auint *v, const auint *w, const auint *k) {
        aint vals[8];
        for(auint cp = 0; cp < 8; cp++) vals[cp] = v[cp];
        for(auint i = 0; i < 8; i++) {
            auint temp = vals[7] + k[i] + w[i];
            temp += S3(vals[4]) + F1(vals[4], vals[5], vals[6]);
            vals[3] += temp;
            vals[7] = temp + S2(vals[0]) + F0(vals[0], vals[1], vals[2]);

            const auint seven = vals[7];
            for(auint cp = 7; cp; cp--) vals[cp] = vals[cp - 1];
            vals[0] = seven; // hopefully the compiler unrolls this for me
        }
        for(auint cp = 0; cp < 8; cp++) v[cp] = vals[cp];
    }
    
    /* Taken directly from the monolithic OpenCL kernel, but I don't need unrolling there, I have plenty of caches */
    void SHA256(auint *hio) {
        const auint IV[8] =  {
            0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
            0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
        };
        auint K[4][16] = {
            {
                0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
                0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
                0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
                0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174
            },
            {
                0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
                0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
                0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
                0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967
            },
            {
                0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
                0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
                0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
                0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070
            },
            {
                0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
                0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
                0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
                0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
            }
        };
        const auint WK[8][8] = {
            {
                0x80000000, 0x00000000, 0x00000000, 0x00000000,
                0x00000000, 0x00000000, 0x00000000, 0x00000000
            },
            {
                0x00000000, 0x00000000, 0x00000000, 0x00000000,
                0x00000000, 0x00000000, 0x00000000, 0x00000200
            },
            {
                0x80000000, 0x01400000, 0x00205000, 0x00005088,
                0x22000800, 0x22550014, 0x05089742, 0xa0000020
            },
            {
                0x5a880000, 0x005c9400, 0x0016d49d, 0xfa801f00,
                0xd33225d0, 0x11675959, 0xf6e6bfda, 0xb30c1549
            },
            {
                0x08b2b050, 0x9d7c4c27, 0x0ce2a393, 0x88e6e1ea,
                0xa52b4335, 0x67a16f49, 0xd732016f, 0x4eeb2e91
            },
            {
                0x5dbf55e5, 0x8eee2335, 0xe2bc5ec2, 0xa83f4394,
                0x45ad78f7, 0x36f3d0cd, 0xd99c05e8, 0xb0511dc7
            },
            {
                0x69bc7ac4, 0xbd11375b, 0xe3ba71e5, 0x3b209ff2,
                0x18feee17, 0xe25ad9e7, 0x13375046, 0x0515089d
            },
            {
                0x4f0d0f04, 0x2627484e, 0x310128d2, 0xc668b434,
                0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF
            }
        };
        auint w[16] = {
            SwapUintBytes(hio[0]), SwapUintBytes(hio[1]),
            SwapUintBytes(hio[2]), SwapUintBytes(hio[3]),
            SwapUintBytes(hio[4]), SwapUintBytes(hio[5]),
            SwapUintBytes(hio[6]), SwapUintBytes(hio[7]),
            SwapUintBytes(hio[8]), SwapUintBytes(hio[9]),
            SwapUintBytes(hio[10]), SwapUintBytes(hio[11]),
            SwapUintBytes(hio[12]), SwapUintBytes(hio[13]),
            SwapUintBytes(hio[14]), SwapUintBytes(hio[15])
        };
        auint hash[8];
        for(auint cp = 0; cp < 8; cp++) hash[cp] = IV[cp];

        SHARound_Set(hash, w, K[0]);
        SHARound_Update(hash, w, K[1]);
        SHARound_Update(hash, w, K[2]);
        SHARound_Update_Last(hash, w, K[3]);
        for(auint el = 0; el < 8; el++) hash[el] += IV[el];
        auint firstHash[8];
        for(auint cp = 0; cp < 8; cp++) firstHash[cp] = hash[cp];
        
        SHAHalfRound_Constant(hash, WK[0], K[0] + 0);
        SHAHalfRound_Constant(hash, WK[1], K[0] + 8);
        SHAHalfRound_Constant(hash, WK[2], K[1] + 0);
        SHAHalfRound_Constant(hash, WK[3], K[1] + 8);
        SHAHalfRound_Constant(hash, WK[4], K[2] + 0);
        SHAHalfRound_Constant(hash, WK[5], K[2] + 8);
        SHAHalfRound_Constant(hash, WK[6], K[3] + 0);

        // Now odd stuff: we do 5/8 of a round...
        for(auint i = 0; i < 4; i++) {
            auint temp = hash[7] + K[3][8 + i] + WK[7][i];
            temp += S3(hash[4]) + F1(hash[4], hash[5], hash[6]);
            hash[3] += temp;
            hash[7] = temp + S2(hash[0]) + F0(hash[0], hash[1], hash[2]);

            const auint seven = hash[7];
            for(auint cp = 7; cp; cp--) hash[cp] = hash[cp - 1];
            hash[0] = seven; // hopefully the compiler unrolls this for me
        }
        for(auint cp = 0; cp < 4; cp++) std::swap(hash[cp], hash[cp + 4]);

        // ... and half of a eight... sort of.
        // Note this is 'temp' with special W,K, no assign to s7.
        hash[7] += 0x420841cc + 0x90BEFFFAU;
        hash[7] += hash[3] + S3(hash[0]) + F1(hash[0], hash[1], hash[2]);
        for(auint el = 0; el < 8; el++) hash[el] += firstHash[el];
        hio[0] = hash[0];
        hio[1] = hash[1];
        hio[2] = hash[2];
        hio[3] = hash[3];
        hio[4] = hash[4];
        hio[5] = hash[5];
        hio[6] = hash[6];
        hio[7] = hash[7];
    }
};
