/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <vector>
#include "../Common/AREN/SerializationBuffers.h"
#include <array>

extern "C" {
#include "../SPH/sph_luffa.h"
#include "../SPH/sph_cubehash.h"
#include "../SPH/sph_shavite.h"
#include "../SPH/sph_simd.h"
#include "../SPH/sph_echo.h"
#include "../SPH/sph_groestl.h"
};



/*! The building blocks of a mining hashing algorithm are always more or less the same.
They are for example luffa, SIMD... this encapsulates a basic building block for use to verify hashed nonces.
The only complication is that some hashers might take headers, some might consume an hash from a previous one...
The head of a chain is different because it consumes an header block which is always 80 bytes and a nonce which is encapsulated somehow,
headers are still generic IntermediateHasherInterface however but they also expose this to set the nonce in advance. */
struct AbstractHeaderHasher {
    virtual ~AbstractHeaderHasher() { }
    virtual std::vector<aubyte> GetHeader(const std::array<aubyte, 80> &input, auint nonce) = 0;
};

/*! Everything that is not an head has it slightly more complicated. Here we consume some bytes in input and return some others in output.
But... there's no nonce to consume.
A further complication is that hashing algorithms used in cryptocurrency are not always "clean" as they're supposed to be.
As long as they stem from real crypto algorithms they just work... in that case, they consume everything and pull out an hash.
Unfortunately, some algorithms don't seem to have a proper definition, therefore I must be able to detect when their input is compatible with implementation. */
struct IntermediateHasherInterface {
    virtual ~IntermediateHasherInterface() { }
    //! \return hash parameter so it can be used as a parameter with no copy.
    virtual std::vector<aubyte>& Hash(std::vector<aubyte> &hash, const std::vector<aubyte> &input) = 0;
    //! Returns true if the size of the input is compatible with the implementation.
    //! Otherwise, the hasher will fail to bind at construction time.
    virtual bool CanMangle(asizei inputByteCount) const = 0;
    // \sa HeaderHasherInterface::GetHashByteCount
    virtual asizei GetHashByteCount() const = 0;
};


struct HLuffa512 : IntermediateHasherInterface, AbstractHeaderHasher {
    std::vector<aubyte> GetHeader(const std::array<aubyte, 80> &input, auint nonce) {
        std::vector<aubyte> noncedBlockHeader(80);
        for(asizei cp = 0; cp < input.size(); cp++) noncedBlockHeader[cp] = input[cp];
        nonce = HTON(nonce);
        memcpy_s(noncedBlockHeader.data() + 76, sizeof(noncedBlockHeader[0]) * noncedBlockHeader.size() - 76, &nonce, sizeof(nonce));
        return noncedBlockHeader;
    }
    std::vector<aubyte>& Hash(std::vector<aubyte> &hash, const std::vector<aubyte> &input) {
        hash.resize(64);
        sph_luffa512_context head;
        sph_luffa512_init(&head);
        sph_luffa512(&head, input.data(), input.size());
        sph_luffa512_close(&head, hash.data());
        return hash;
    }
    bool CanMangle(asizei inputByteCount) const { return 80 == inputByteCount; }
    asizei GetHashByteCount() const { return 512 / 8; }
};


struct HShaVite512 : IntermediateHasherInterface, AbstractHeaderHasher {
    std::vector<aubyte> GetHeader(const std::array<aubyte, 80> &input, auint nonce) {
        std::vector<aubyte> noncedBlockHeader(80);
        for(asizei cp = 0; cp < input.size(); cp++) noncedBlockHeader[cp] = input[cp];
        nonce = HTON(nonce);
        memcpy_s(noncedBlockHeader.data() + 76, sizeof(noncedBlockHeader[0]) * noncedBlockHeader.size() - 76, &nonce, sizeof(nonce));
        return noncedBlockHeader;
    }
    std::vector<aubyte>& Hash(std::vector<aubyte> &hash, const std::vector<aubyte> &input) {
        hash.resize(64);
        sph_shavite512_context head;
        sph_shavite512_init(&head);
        sph_shavite512(&head, input.data(), input.size());
        sph_shavite512_close(&head, hash.data());
        return hash;
    }
    bool CanMangle(asizei inputByteCount) const { return 80 == inputByteCount; }
    asizei GetHashByteCount() const { return 512 / 8; }
};


struct CubeHash512 : IntermediateHasherInterface {
    std::vector<aubyte>& Hash(std::vector<aubyte> &hash, const std::vector<aubyte> &input) {
        hash.resize(64);
        sph_cubehash512_context head;
        sph_cubehash512_init(&head);
        sph_cubehash512(&head, input.data(), input.size());
        sph_cubehash512_close(&head, hash.data());
        return hash;
    }
    bool CanMangle(asizei inputByteCount) const { return inputByteCount == 64; }
    asizei GetHashByteCount() const { return 64; }
};


struct ShaVite512 : IntermediateHasherInterface {
    std::vector<aubyte>& Hash(std::vector<aubyte> &hash, const std::vector<aubyte> &input) {
        hash.resize(64);
        sph_shavite512_context head;
        sph_shavite512_init(&head);
        sph_shavite512(&head, input.data(), input.size());
        sph_shavite512_close(&head, hash.data());
        return hash;
    }
    bool CanMangle(asizei inputByteCount) const { return inputByteCount == 64; }
    asizei GetHashByteCount() const { return 64; }
};


struct SIMD512 : IntermediateHasherInterface {
    std::vector<aubyte>& Hash(std::vector<aubyte> &hash, const std::vector<aubyte> &input) {
        hash.resize(64);
        sph_simd512_context head;
        sph_simd512_init(&head);
        sph_simd512(&head, input.data(), input.size());
        sph_simd512_close(&head, hash.data());
        return hash;
    }
    bool CanMangle(asizei inputByteCount) const { return inputByteCount == 64; }
    asizei GetHashByteCount() const { return 64; }
};


struct ECHO512 : IntermediateHasherInterface {
    std::vector<aubyte>& Hash(std::vector<aubyte> &hash, const std::vector<aubyte> &input) {
        hash.resize(64);
        sph_echo512_context head;
        sph_echo512_init(&head);
        sph_echo512(&head, input.data(), input.size());
        sph_echo512_close(&head, hash.data());
        return hash;
    }
    bool CanMangle(asizei inputByteCount) const { return inputByteCount == 64; }
    asizei GetHashByteCount() const { return 64; }
};


struct HGroestl512 : AbstractHeaderHasher, IntermediateHasherInterface {
    std::vector<aubyte> GetHeader(const std::array<aubyte, 80> &input, auint nonce) {
        std::vector<aubyte> noncedBlockHeader(80);
        for(asizei cp = 0; cp < input.size(); cp++) noncedBlockHeader[cp] = input[cp];
        nonce = HTON(nonce);
        memcpy_s(noncedBlockHeader.data() + 76, sizeof(noncedBlockHeader[0]) * noncedBlockHeader.size() - 76, &nonce, sizeof(nonce));
        return noncedBlockHeader;
    }
    std::vector<aubyte>& Hash(std::vector<aubyte> &hash, const std::vector<aubyte> &input) {
        hash.resize(64);
        sph_groestl512_context head;
        sph_groestl512_init(&head);
        sph_groestl512(&head, input.data(), input.size());
        sph_groestl512_close(&head, hash.data());
        return hash;
    }
    bool CanMangle(asizei inputByteCount) const { return 80 == inputByteCount; }
    asizei GetHashByteCount() const { return 512 / 8; }
};
