/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <array>
#include "SerializationBuffers.h"
#include "arenDataTypes.h"


namespace hashing {

/*! A bit of warning about the LenType parameter.
The SHA standard mandates the use of a 64-bit length and SHA blocks must be sized according to that length.
However, some implementation only use 32bit length. They still reserve 8 bytes, but only use 4.
Because of bit order packing, I cannot just write a 64-bit because for example, 
len=0x1234ABCD					len=0x000000001234ABCD
written as (last eight bytes), big endian
CDAB4312						CDAB431200000000
Which is a very different thing. Therefore, before writing the length, it will be cast to the appropriate
type. Blocks are still sized with an 8 bit length anyway. */
template<typename LenType>
class VariableLengthSHA256 {
private:
	std::array<auint, 8> h;
	std::array<aubyte, 64> pad; //!< used to process the last packet without "appending" for real, size of a block.
	aulong bytesProcessed;
	static auint SigmaO(auint v) { return _rotr(v,  7) ^ _rotr(v, 18) ^    (v >>  3); };
	static auint SigmaI(auint v) { return _rotr(v, 17) ^ _rotr(v, 19) ^    (v >> 10); };
	static auint SumO(auint v)   { return _rotr(v,  2) ^ _rotr(v, 13) ^ _rotr(v, 22); };
	static auint SumI(auint v)   { return _rotr(v,  6) ^ _rotr(v, 11) ^ _rotr(v, 25); };
	static auint Ch(auint x, auint y, auint z)  { return (x & y) ^ (~x & z); };
	static auint Maj(auint x, auint y, auint z) { return (x & y) ^ ( x & z) ^ (y & z); };
public:
	asizei GetBlockSize() const { return 512 / 8; /* also size of pad */ }
	asizei GetHashSize() const  { return sizeof(h); };
	typedef std::array<aubyte, 32> Digest;
	Digest& GetHash(Digest &hashBE) const {
		DestinationStream serializer(hashBE.data(), sizeof(hashBE));
		for(asizei loop = 0; loop < h.size(); loop++) serializer<<h[loop];
		return hashBE;
	}
	Digest& GetHashLE(Digest &hashLE) const {
		memcpy_s(hashLE.data(), sizeof(hashLE), h.data(), sizeof(h));
		return hashLE;
	}
	DestinationStream& GetHash(DestinationStream &serializer) const {
		for(asizei loop = 0; loop < h.size(); loop++) serializer<<h[loop];
		return serializer;
	}
	void Restart() {
		auint hstart[8] = {
			0x6a09e667,  //2^32 times the square root of the first 8 primes 2..19
			0xbb67ae85,
			0x3c6ef372,
			0xa54ff53a,
			0x510e527f,
			0x9b05688c, 
			0x1f83d9ab,
			0x5be0cd19
		};
		memcpy_s(h.data(), sizeof(h), hstart, sizeof(hstart));
		bytesProcessed = 0;
	}
	void BlockProcessing(const aubyte *chunk) {
		const auint K[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
		};
		std::array<auint, 64> w;
		const auint *cbytes = reinterpret_cast<const auint*>(chunk);
		auint *wbytes = w.data();
		for(size_t cp = 0; cp < 16; cp++) wbytes[cp] = HTON(cbytes[cp]);
		for(size_t cp = 16; cp < 64; cp++) {
			auint so = SigmaO(w[cp - 15]);
			auint si = SigmaI(w[cp -  2]);
			w[cp] = w[cp - 16] + so + w[cp - 7] + si;
		}
		auint a = h[0],  b  = h[1];
		auint c = h[2],  d  = h[3];
		auint e = h[4],  f  = h[5];
		auint g = h[6],  hp = h[7];
		for(size_t inner = 0; inner < 64; inner++) {
			auint t1 = hp + SumI(e) + Ch(e, f, g) + K[inner] + w[inner];
			auint t2 = SumO(a) + Maj(a, b, c);			
			hp = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		// Add the hash to result so far.
		h[0] += a;  h[1] += b;
		h[2] += c;  h[3] += d;
		h[4] += e;	h[5] += f;
		h[6] += g;	h[7] += hp;
		bytesProcessed += 16 * sizeof(auint);
	}
	/*! The final block of data can be partial, so you pass an explicit byte count to mangle.
	This block will terminate the stream of data, but sometimes you want to put the SHA256 length
	terminator on the following block. In this case, pad should be true. The gap after the
	terminator to the length is filled with 0s as specified by SHA256 standard.
	\note this can really mangle multiple blocks, as long as the last is partial and is the
	closing one. */
	void EndBlocks(const aubyte *msg, asizei count) {
		while(count >= GetBlockSize()) {
			BlockProcessing(msg);
			msg += GetBlockSize();
			count -= GetBlockSize();
		}
		memcpy_s(pad.data(), sizeof(pad), msg, count);
		pad[count] = 0x80;
		memset(pad.data() + count + 1, 0, sizeof(pad) - count - 1);
		if((bytesProcessed + count) * 8 > LenType(-1)) throw std::exception("Message is too long for this bit count length!");
		if((bytesProcessed + count) * 8 <= LenType(bytesProcessed + count)) throw std::exception("Message is too long for this bit count length!");
		const LenType bitLen = LenType((bytesProcessed + count) * 8);
		if(count + sizeof(aulong) < sizeof(pad)) {
			asizei off = pad.size() - sizeof(LenType);
			DestinationStream serializer(pad.data() + off, sizeof(pad) - off);
			serializer<<bitLen;
		}
		BlockProcessing(pad.data());
		if(count + sizeof(aulong) < sizeof(pad)) return;
		// If here, either the length overflows or we have been explicitly asked to put it in
		// another block. No real difference.
		memset(pad.data(), 0, sizeof(pad));
		asizei off = sizeof(pad) - sizeof(LenType);
		DestinationStream serializer(pad.data() + off, sizeof(LenType));
		serializer<<bitLen;
		BlockProcessing(pad.data());
	}
	VariableLengthSHA256() : bytesProcessed(0) {
		if(sizeof(LenType) > sizeof(aulong)) throw std::exception("SHA256 standards admits length up to 64bits wide.");
		Restart();
	}
	VariableLengthSHA256(const aubyte *msg, asizei count) : bytesProcessed(0) {
		if(sizeof(LenType) > sizeof(aulong)) throw std::exception("SHA256 standards admits length up to 64bits wide.");
		Restart();
		EndBlocks(msg, count);
	}
	VariableLengthSHA256(const VariableLengthSHA256 &from, bool hash = false) : bytesProcessed(0) {
		if(sizeof(LenType) > sizeof(aulong)) throw std::exception("SHA256 standards admits length up to 64bits wide.");
		if(hash) {
			Restart();
			Digest prev;
			from.GetHash(prev);
			EndBlocks(prev.data(), prev.size());
		}
		else {
			std::copy(from.pad.cbegin(), from.pad.cend(), pad.begin());
			std::copy(from.h.cbegin(), from.h.cend(), h.begin());
			bytesProcessed = from.bytesProcessed;
		}
	}
};


typedef VariableLengthSHA256<auint> BTCSHA256;
typedef VariableLengthSHA256<aulong> SHA256;


template<auint PASSES_2N>
void Salsa20(auint B[16], const auint Bx[16]) {
	auint w[16];
	for(asizei loop = 0; loop < 16; loop++) {
		B[loop] ^= Bx[loop];
		w[loop] = B[loop];
	}
	const auint movement[4] = { 7, 9, 13, 18 };
	for(auint round = 0; round < PASSES_2N; round += 2) {
		for(asizei outer = 0; outer < 4; outer++) { // "columns"
			for(asizei inner = 0; inner < 4; inner++) {
				const asizei dst = ((outer + 1) * 4 + inner * 5) % 16;
				const asizei ai = (dst + 12) % 16;
				const asizei bi = (dst +  8) % 16;
				w[dst] ^= _rotl(w[ai] + w[bi], movement[outer]);
			}
		}
		// the "rows" are not quite as easy, I just write them explicitly until I figure out the rule
		w[ 1] ^= _rotl(w[ 0] + w[ 3],  7);
		w[ 6] ^= _rotl(w[ 5] + w[ 4],  7);
		w[11] ^= _rotl(w[10] + w[ 9],  7);
		w[12] ^= _rotl(w[15] + w[14],  7);

		w[ 2] ^= _rotl(w[ 1] + w[ 0],  9);
		w[ 7] ^= _rotl(w[ 6] + w[ 5],  9);
		w[ 8] ^= _rotl(w[11] + w[10],  9);
		w[13] ^= _rotl(w[12] + w[15],  9);

		w[ 3] ^= _rotl(w[ 2] + w[ 1], 13);
		w[ 4] ^= _rotl(w[ 7] + w[ 6], 13);
		w[ 9] ^= _rotl(w[ 8] + w[11], 13);
		w[14] ^= _rotl(w[13] + w[12], 13);

		w[ 0] ^= _rotl(w[ 3] + w[ 2], 18);
		w[ 5] ^= _rotl(w[ 4] + w[ 7], 18);
		w[10] ^= _rotl(w[ 9] + w[ 8], 18);
		w[15] ^= _rotl(w[14] + w[13], 18);
	}
	for(asizei loop = 0; loop < 16; loop++) B[loop] += w[loop];
}


template<auint PASSES_2N>
void DoubleSalsa20(std::array<auint, 32> &B) {
	auint *raw = B.data();
	Salsa20<PASSES_2N>(raw,      raw + 16);
	Salsa20<PASSES_2N>(raw + 16, raw     );
}


/*! Legacy miners have this function, apparently PBKDF2 is some sort of standard.
The comments were clearly let to rot so I have no idea what it's supposed to do.
All I care about is that this function has been tested to be equivalent to miner's
implementation. They call it PBKDF2_SHA256_80_128, I let it be templated for some reason. */
template<class HASHER>
void PBKDF2(std::array<auint, 32> &output, const std::array<auint, 20> &passwd)
{
	auint pad[16];
	auint innerpad[11], passwdpad[12], outerpad[8];
	memset(innerpad, 0, sizeof(innerpad));      innerpad [0] = 0x00000080;       innerpad [10] = 0xa0040000;
	memset(passwdpad, 0, sizeof(passwdpad));    passwdpad[0] = 0x00000080;       passwdpad[11] = 0x80020000;
	memset(outerpad, 0, sizeof(outerpad));      outerpad [0] = 0x80000000;       outerpad [ 7] = 0x00000300;

	HASHER tstate;
	tstate.BlockProcessing(reinterpret_cast<const aubyte*>(passwd.data()));
	memcpy(pad, passwd.data() + 16, 16);
	memcpy(pad+4, passwdpad, 48);
	tstate.BlockProcessing(reinterpret_cast<const aubyte*>(pad));

	HASHER::Digest ihashDWORD;
	tstate.GetHashLE(ihashDWORD);
	const auint *ihash = reinterpret_cast<const auint*>(ihashDWORD.data());
	
	HASHER hi;
	for (auint i = 0; i <  8; i++) pad[i] = ihash[i] ^ 0x36363636;
	for (auint i = 8; i < 16; i++) pad[i] = 0x36363636;

	// Problem: my SHA256 does not support conditional byte order swap. No problem...
	for(auint i = 0; i < 16; i++) pad[i] = HTON(pad[i]);
	hi.BlockProcessing(reinterpret_cast<const aubyte*>(pad));
	hi.BlockProcessing(reinterpret_cast<const aubyte*>(passwd.data()));

	// Problem: at this point I should overwrite part (but a uint) of the mangled buffer.
	// My implementation does not allow to access it, much less write or reuse it for something else.
	// Let the compiler figure out what to do.
	auint hibuf[16];
	for(auint cp = 0; cp < 4; cp++) hibuf[cp] = HTON(passwd[16 + cp]);
	// hibuf[4] is set in a loop below
	for(auint cp = 0; cp < 11; cp++) hibuf[cp + 5] = HTON(innerpad[cp]);

	HASHER ho;
	for (auint i = 0; i <  8; i++) pad[i] = ihash[i] ^ 0x5c5c5c5c;
	for (auint i = 8; i < 16; i++) pad[i] = 0x5c5c5c5c;
	for(auint i = 0; i < 16; i++) pad[i] = HTON(pad[i]);
	ho.BlockProcessing(reinterpret_cast<const aubyte*>(pad));
	auint hobuf[16];
	for(auint cp = 8; cp < 16; cp++) hobuf[cp] = outerpad[cp - 8];
	
	for(auint block = 0; block < 4; block++) {		
		HASHER istate(hi), ostate(ho);
		hibuf[4] = block + 1;
		auint endian[16];
		for(auint cp = 0; cp < 16; cp++) endian[cp] = HTON(hibuf[cp]);
		istate.BlockProcessing(reinterpret_cast<const aubyte*>(endian));

		std::array<aubyte, 32> ugly;
		const auint *terrible = reinterpret_cast<const auint*>(ugly.data());
		istate.GetHashLE(ugly);
		memcpy_s(hobuf, sizeof(hobuf), ugly.data(), sizeof(ugly));
		for(auint cp = 0; cp < 16; cp++) endian[cp] = HTON(hobuf[cp]);
		ostate.BlockProcessing(reinterpret_cast<const aubyte*>(endian));
		ostate.GetHashLE(ugly);
		for(auint cp = 0; cp < 8; cp++) output[block * 8 + cp] = HTON(terrible[cp]);
	}
}


/*! Yet another version of the above. Writes result to a different place and uses salt (scrypt X parameters). */
template<class HASHER>
void PBKDF2(std::array<aubyte, 8*4> &output, const std::array<auint, 20> &passwd, const std::array<auint, 32> &salt)
{
	auint ihash[8], pad[16];
	auint passwdpad[12], outerpad[8];
	memset(passwdpad, 0, sizeof(passwdpad));
	memset(outerpad, 0, sizeof(outerpad));
	passwdpad[ 0] = 0x00000080;
	passwdpad[11] = 0x80020000;
	outerpad[0] = 0x80000000;
	outerpad[7] = 0x00000300;

	{
		HASHER tstate;
		tstate.BlockProcessing(reinterpret_cast<const aubyte*>(passwd.data()));
		memcpy_s(pad, sizeof(pad), passwd.data() + 16, 4 * 4);
		memcpy_s(pad + 4, sizeof(pad) - 4 * 4, passwdpad, sizeof(passwdpad));
		tstate.BlockProcessing(reinterpret_cast<const aubyte*>(pad));
		HASHER::Digest hash;
		tstate.GetHashLE(hash);
		memcpy_s(ihash, sizeof(ihash), hash.data(), sizeof(hash[0]) * hash.size());
	}

	HASHER ostate;
	for(auint init = 0; init <  8; init++) pad[init] = HTON(auint(ihash[init] ^ 0x5c5c5c5c));
	for(auint init = 8; init < 16; init++) pad[init] = HTON(auint(0x5c5c5c5c));
	ostate.BlockProcessing(reinterpret_cast<const aubyte*>(pad));

	{
		HASHER tstate;
		for (auint init = 0; init <  8; init++) pad[init] = HTON(auint(ihash[init] ^ 0x36363636));
		for (auint init = 8; init < 16; init++) pad[init] = HTON(auint(0x36363636));
		tstate.BlockProcessing(reinterpret_cast<const aubyte*>(pad));
		tstate.BlockProcessing(reinterpret_cast<const aubyte*>(salt.data()));
		tstate.BlockProcessing(reinterpret_cast<const aubyte*>(salt.data() + 16));

		auint finalBlock[16];
		memset(finalBlock, 0, sizeof(finalBlock));
		finalBlock[ 0] = HTON(auint(0x00000001));
		finalBlock[ 1] = HTON(auint(0x80000000));
		finalBlock[15] = HTON(auint(0x00000620));
		tstate.BlockProcessing(reinterpret_cast<const aubyte*>(finalBlock));

		HASHER::Digest hash;
		tstate.GetHashLE(hash);
		memcpy_s(pad, sizeof(pad), hash.data(), sizeof(hash));
		memcpy_s(pad + 8, sizeof(pad) - 8 * 4, outerpad, sizeof(outerpad));
	}
	for(auint swap = 0; swap < 16; swap++) pad[swap] = HTON(pad[swap]);
	ostate.BlockProcessing(reinterpret_cast<const aubyte*>(pad));
	hashing::SHA256::Digest hash;
	ostate.GetHashLE(hash);
	memcpy_s(output.data(), sizeof(output), hash.data(), sizeof(hash));
}


template<auint N>
void Scrypt(std::array<aubyte, 32> &result, const std::array<auint, 20> &header, std::array<auint, 4 * 8 * N> &pad) {
	std::array<auint, 32> X;
	hashing::PBKDF2<hashing::SHA256>(X, header);
	for(auint loop = 0; loop < N; loop++) {
		for(asizei cp = 0; cp < 32; cp++) pad[loop * 32 + cp] = X[cp];
		hashing::DoubleSalsa20<8>(X);
	}
	for(auint loop = 0; loop < N; loop++) {
		const auint j(X[16] % N);
		for(auint k = 0; k < 32; k++) X[k] ^= pad[j * 32 + k]; // let the compiler figure out what to do, don't mess with uint64
		hashing::DoubleSalsa20<8>(X);
	}
	hashing::PBKDF2<hashing::SHA256>(result, header, X);
}

}
