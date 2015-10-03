#pragma once
#include "HashBlocks.h"
#include <memory>
#include "bsty_miner/yescrypt.h"


class BSTYYescrypt : public IntermediateHasherInterface, public AbstractHeaderHasher {
public:
    // const asizei N = 2048;
    // const asizei r = 8;
    // const asizei p = 1;
    explicit BSTYYescrypt() { }
    std::vector<aubyte> GetHeader(const std::array<aubyte, 80> &input, auint nonce) {
        std::vector<aubyte> copy(80);
        for(asizei i = 0; i < input.size() / 4; i++) {
            for(asizei cp = 0; cp < 4; cp++) copy[i * 4 + cp] = input[i * 4 + 3 - cp];
        }
        memcpy_s(copy.data() + 76, sizeof(copy[0]) * copy.size() - 76, &nonce, sizeof(nonce));
        std::swap(copy[76], copy[79]);
        std::swap(copy[77], copy[78]);
        return copy;
    }
    std::vector<aubyte>& Hash(std::vector<aubyte> &hash, const std::vector<aubyte> &input) {
        hash.resize(32);
        yescrypt_hash_sp(reinterpret_cast<const abyte*>(input.data()), reinterpret_cast<abyte*>(hash.data()));
        return hash;
    }

    virtual bool CanMangle(asizei inputByteCount) const { return inputByteCount == 80; }
    asizei GetHashByteCount() const { return 32; }
};
