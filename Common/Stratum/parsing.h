/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <json/json.h>
#include "messages.h"
#include <memory>
#include "../ArenDataTypes.h"

namespace stratum {

/*! Contains objects mangling JSON-RPC objects to the proper data structures
as defined in messages.h */
namespace parsing {

struct AbstractParser {
	/*! Decode a hex character to its value, with safety checks.
	\returns 0 <= ret < 16 */
	static __int8 DecodeHEX(char c) {
		c = toupper(c);
		if(c >= '0' && c <= '9') return c - '0';
		if(c >= 'A' && c <= 'F') return c - 'A' + 10;
		throw std::exception("Hexadecimal string contains invalid character.");
		return c;
	}

	/* Decode a string made of hex digits in an array of uint8 being half as long.
	\returns The vector used as destination. */
	static std::vector<aubyte>& DecodeHEX(std::vector<aubyte> &dst, const char *hex, asizei len) {
		if(len % 1) throw std::exception("Hexadecimal string truncated.");
		dst.resize(len / 2);
		for(size_t scan = 0; scan < len; scan += 2) {
			dst[scan / 2] = DecodeHEX(hex[scan]) << 4 | DecodeHEX(hex[scan + 1]); // + is ok too
		}
		return dst;
	}
	static std::vector<unsigned __int8>& DecodeHEX(std::vector<unsigned __int8> &dst, const std::string &hex) {
		return DecodeHEX(dst, hex.c_str(), hex.size());
	}
	/*! Takes a few bytes and produces a HEX string. */
	static std::string EncodeToHEX(const aubyte *stream, asizei count) {
		std::unique_ptr<char[]> enc(new char[count * 2 + 1]);
		char *dst = enc.get();
		char *digits = "0123456789abcdef";
		for(asizei loop = 0; loop < count; loop++) {
			*dst = digits[stream[loop] >> 4];
			dst++;
			*dst = digits[stream[loop] & 0x0F];
			dst++;
		}
		*dst = 0;
		return std::string(enc.get());
	}
	static std::string EncodeToHEX(const std::vector<aubyte> &stream) {
		return EncodeToHEX(stream.data(), stream.size());
	}
	template<typename Scalar>
	static std::string EncodeToHEX(Scalar value) {
		/* When we decoded this integer we slapped it to LE */
		value = HTON(value);
		return EncodeToHEX(reinterpret_cast<aubyte*>(&value), sizeof(value));
	}
	template<size_t SZ>
	static std::array<unsigned __int8, SZ>& DecodeHEX(std::array<unsigned __int8, SZ> &dst, const std::string &hex) {
		if(hex.length() % 1) throw std::exception("Hexadecimal string truncated.");
		if(hex.size() < hex.length() / 2) throw std::exception("Hexadecimal string is too long, overflows available constant bits.");
		for(size_t scan = 0; scan < hex.length(); scan += 2) {
			dst[scan / 2] = DecodeHEX(hex[scan]) << 4 | DecodeHEX(hex[scan + 1]); // + is ok too
		}
		return dst;
	}
	template<typename Integer>
	static Integer DecodeHEX(const std::string &hex) {
		Integer ret(0);
		if(hex.length() / 2 > sizeof(Integer)) throw std::exception("Too many bits to pack.");
		size_t shift = 0;
		for(size_t scan = 0; scan < hex.length(); scan++) {
			char c = hex[hex.length() - 1 - scan];
			ret |= DecodeHEX(c) << shift;
			//! \todo this assumes the target machine is little endian.
			//! so when the message is big-endian, we also swap
			//! is it the correct thing to do?
			shift += 4;
		}
		return ret;
	}

	static std::string ToString(const Json::Value &something) {
		if(something.isNull() || something.isArray() || something.isObject()) return std::string("");
		if(something.isString()) return something.asString();
		const char *blah = something.toStyledString().c_str();
		if(blah[0] == '"') blah++;
		size_t len = strlen(blah);
		if(blah[len - 1] == '"') len--;
		return std::string(blah, blah + len);
	}
	const std::string name;
	AbstractParser(const char *str) : name(str) { }
};


struct MiningSubscribe : public AbstractParser {
	typedef MiningSubscribeResponse Product;
	MiningSubscribe() : AbstractParser("mining.subscribe") { }
	Product* Mangle(const Json::Value &root) {
		if(!root.isArray()) throw std::exception(".result should be an array.");
		if(root.size() != 3) throw std::exception("mining.subscribe .result array must count 3 elements.");
		auto session = root[0U];
		auto extraNonceOne = root[1];
		auto extraNonceTwoSZ = root[2];
		if(session.isArray() == false || session.size() != 2) throw std::exception("mining.subscribe .result[0] array must count 2 elements.");
		if(session[0U].isString() == false || session[0U].asString() != "mining.notify") throw std::exception("mining.subscribe .result[0][0] is not \"mining.notify\"");
		if(session[1].isString() == false || session[1].asString().empty()) throw std::exception("mining.subscribe .result[0][1] is not a valid session string.");
		size_t sz = 0;
		if(extraNonceTwoSZ.isUInt()) sz = extraNonceTwoSZ.asUInt();
		else if(extraNonceTwoSZ.isInt()) {
			int temp = extraNonceTwoSZ.asInt();
			if(temp < 0) throw std::exception("Extra nonce size is negative.");
			sz = static_cast<size_t>(temp);
		}
		else if(extraNonceTwoSZ.isString()) {
			const char *chars = extraNonceTwoSZ.asCString();
			for(size_t loop = 0; chars[loop]; loop++) {
				if(chars[loop] < '0' || chars[loop] > '9') throw std::exception("Extra nonce size should be a positive integer.");
			}
			sz = strtoul(chars, NULL, 10);
		}
		else throw std::exception("mining.subscribe .result[3] is not a valid extra size.");
		if(extraNonceOne.isString() == false) throw std::exception("mining.subscribe .result[2] is not a valid nonce1 string.");
		//! \note ExtraNonce1 strings can apparently be the empty string. It's ok.
		std::unique_ptr<Product> ret(new Product(session[1].asString(), extraNonceTwoSZ.asUInt()));
		DecodeHEX(ret->extraNonceOne, extraNonceOne.asString());
		return ret.release();
	}
};


struct MiningAuthorize : public AbstractParser {
	typedef MiningAuthorizeResponse Product;
	MiningAuthorize() : AbstractParser("mining.authorize") { }
	Product* Mangle(const Json::Value &root) {
		return new Product(root.asBool());
	}
};


struct MiningSubmit : public AbstractParser {
	typedef MiningSubmitResponse Product;
	MiningSubmit() : AbstractParser("mining.submit") { }
	Product* Mangle(const Json::Value &root) {
		return new Product(root.asBool());
	}
};


struct ClientGetVersion : public AbstractParser {
	typedef ClientGetVersionRequest Product;
	ClientGetVersion() : AbstractParser("client.get_version") { }
	Product* Mangle(const std::string &id, const Json::Value &params) {
		return new Product(id);
	}
};


struct MiningSetDifficulty : public AbstractParser {
	typedef MiningSetDifficultyNotify Product;
	MiningSetDifficulty() : AbstractParser("mining.set_difficulty") { }
	Product* Mangle(const Json::Value &params) {
		const Json::Value &value(params[0U]);
		double diff;
		if(value.isDouble()) diff = value.asDouble();
		else if(value.isUInt()) diff = value.asInt(); // hopefully it won't truncate
		else if(value.isInt()) diff = value.asInt();
		else throw std::exception("difficulty value must is not numeric!");
		return new Product(diff);
	}
};


/*! \todo better naming structure */
struct MiningNotifyParser : public AbstractParser {
	typedef MiningNotify Product;
	MiningNotifyParser() : AbstractParser("mining.notify") { }
	Product* Mangle(const Json::Value &params) {
		if(params[8].isBool() == false) throw std::exception("restart flag is not a bool");
		const bool restart = params[8].asBool();
		const __int32 ntime = DecodeHEX<__int32>(ToString(params[7]));
		const __int32 nbit = DecodeHEX<__int32>(ToString(params[6]));
		const __int32 blockVersion = DecodeHEX<__int32>(ToString(params[5]));
		const std::string job(ToString(params[0U]));
		std::unique_ptr<MiningNotify> ret(new MiningNotify(job, blockVersion, nbit, ntime, restart));
		DecodeHEX(ret->prevHash, params[1].asString());
		DecodeHEX(ret->coinBaseOne, params[2].asString());
		DecodeHEX(ret->coinBaseTwo, params[3].asString());
		auto &merkles(params[4]);
		if(merkles.isArray() == false) throw std::exception("Merkle array missing");
		ret->merkles.resize(merkles.size()); // notice: it can be 0 length, it's valid.
		for(size_t loop = 0; loop < merkles.size(); loop++) DecodeHEX(ret->merkles[loop].hash, merkles[loop].asString());
		return ret.release();
	}
};


}
}
