/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <array>

namespace btc {

struct MerkleRoot {
	std::array<unsigned __int8, 32> hash;
};

struct BlockHash {
	std::array<unsigned __int8, 32> hash;
};


}
