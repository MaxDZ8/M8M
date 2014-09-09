/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <rapidjson/document.h> // Value
#include "messages.h"
#include <memory>
#include "../AREN/ArenDataTypes.h"

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

	static std::string ToString(const rapidjson::Value &something) {
		// Maybe it would be just better to pretty-print everything here and be done with it? Is that for documents only
		using namespace rapidjson;
		switch(something.GetType()) {
		case kNullType:
		case kArrayType:
		case kObjectType: return std::string("");
		case kFalseType: return std::string("false");
		case kTrueType: return std::string("true");
		case kStringType: return std::string(something.GetString(), something.GetStringLength());
		// kNumberType = 6
		}
		if(something.IsInt()) return std::to_string(something.GetInt());
		else if(something.IsUint()) return std::to_string(something.GetUint());
		else if(something.IsInt64()) return std::to_string(something.GetInt64());
		else if(something.IsUint64()) return std::to_string(something.GetUint64());
		return std::to_string(something.GetDouble());
	}
	const std::string name;
	AbstractParser(const char *str) : name(str) { }
};


struct MiningSubscribe : public AbstractParser {
	typedef MiningSubscribeResponse Product;
	MiningSubscribe() : AbstractParser("mining.subscribe") { }
	Product* Mangle(const rapidjson::Value &root) {
		if(!root.IsArray()) throw std::exception(".result should be an array.");
		if(root.Size() != 3) throw std::exception("mining.subscribe .result array must count 3 elements.");
		auto &session(root[0U]);
		auto &extraNonceOne(root[1]);
		auto &extraNonceTwoSZ(root[2]);
		if(session.IsArray() == false || session.Size() != 2) throw std::exception("mining.subscribe .result[0] array must count 2 elements.");
		if(session[0U].IsString() == false || std::string(session[0U].GetString(), session[0u].GetStringLength()) != "mining.notify") throw std::exception("mining.subscribe .result[0][0] is not \"mining.notify\"");
		if(session[1].IsString() == false || session[1].GetStringLength() == 0) throw std::exception("mining.subscribe .result[0][1] is not a valid session string.");
		size_t sz = 0;
		if(extraNonceTwoSZ.IsUint()) sz = extraNonceTwoSZ.GetUint();
		else if(extraNonceTwoSZ.IsInt()) {
			int temp = extraNonceTwoSZ.GetInt();
			if(temp < 0) throw std::exception("Extra nonce size is negative.");
			sz = static_cast<size_t>(temp);
		}
		else if(extraNonceTwoSZ.IsString()) {
			const char *chars = extraNonceTwoSZ.GetString();
			for(size_t loop = 0; extraNonceTwoSZ.GetStringLength(); loop++) {
				if(chars[loop] < '0' || chars[loop] > '9') throw std::exception("Extra nonce size should be a positive integer.");
			}
			sz = strtoul(chars, NULL, 10);
		}
		else throw std::exception("mining.subscribe .result[3] is not a valid extra size.");
		if(extraNonceOne.IsString() == false) throw std::exception("mining.subscribe .result[2] is not a valid nonce1 string.");
		//! \note ExtraNonce1 strings can apparently be the empty string. It's ok.
		auto mkString = [](const rapidjson::Value &v) { return std::string(v.GetString(), v.GetStringLength()); };
		std::unique_ptr<Product> ret(new Product(mkString(session[1]), extraNonceTwoSZ.GetUint()));
		DecodeHEX(ret->extraNonceOne, mkString(extraNonceOne));
		return ret.release();
	}
};


struct MiningAuthorize : public AbstractParser {
	typedef MiningAuthorizeResponse Product;
	MiningAuthorize() : AbstractParser("mining.authorize") { }
	Product* Mangle(const rapidjson::Value &root) {
		if(root.IsNull()) return new Product(MiningAuthorizeResponse::ar_notRequired);
		if(!root.IsBool()) throw std::exception("mining.authorize .result is not a valid authorization response.");
		return new Product(root.GetBool()? MiningAuthorizeResponse::ar_pass : MiningAuthorizeResponse::ar_bad);
	}
};


struct MiningSubmit : public AbstractParser {
	typedef MiningSubmitResponse Product;
	MiningSubmit() : AbstractParser("mining.submit") { }
	Product* Mangle(const rapidjson::Value &root) {
		if(root.IsBool() == false) throw std::exception("mining.submit, result is not a boolean.");
		return new Product(root.GetBool());
	}
};


struct ClientGetVersion : public AbstractParser {
	typedef ClientGetVersionRequest Product;
	ClientGetVersion() : AbstractParser("client.get_version") { }
	Product* Mangle(const std::string &id, const rapidjson::Value &params) {
		return new Product(id);
	}
};


struct MiningSetDifficulty : public AbstractParser {
	typedef MiningSetDifficultyNotify Product;
	MiningSetDifficulty() : AbstractParser("mining.set_difficulty") { }
	Product* Mangle(const rapidjson::Value &params) {
		const rapidjson::Value &value(params[0U]);
		double diff;
		if(value.IsDouble()) diff = value.GetDouble();
		else if(value.IsUint()) diff = value.GetInt();
		else if(value.IsInt()) diff = value.GetInt();
		else if(value.IsUint64() || value.IsInt64()) throw std::exception("64-bit difficulty, not supported!");
		else throw std::exception("difficulty value must is not numeric!");
		return new Product(diff);
	}
};


/*! \todo better naming structure */
struct MiningNotifyParser : public AbstractParser {
	typedef MiningNotify Product;
	MiningNotifyParser() : AbstractParser("mining.notify") { }
	Product* Mangle(const rapidjson::Value &params) {
		if(params.IsArray() == false || params.Size() != 9) throw std::exception("mining.notify message not array or not having 8 entries.");
		if(params[8].IsBool() == false) throw std::exception("restart flag is not a bool");
		const bool restart = params[8].GetBool();
		const __int32 ntime = DecodeHEX<__int32>(ToString(params[7]));
		const __int32 nbit = DecodeHEX<__int32>(ToString(params[6]));
		const __int32 blockVersion = DecodeHEX<__int32>(ToString(params[5]));
		const std::string job(ToString(params[0U]));
		std::unique_ptr<MiningNotify> ret(new MiningNotify(job, blockVersion, nbit, ntime, restart));
		DecodeHEX(ret->prevHash, params[1].GetString());
		DecodeHEX(ret->coinBaseOne, params[2].GetString(), params[2].GetStringLength());
		DecodeHEX(ret->coinBaseTwo, params[3].GetString(), params[3].GetStringLength());
		auto &merkles(params[4]);
		if(merkles.IsArray() == false) throw std::exception("Merkle array missing");
		ret->merkles.resize(merkles.Size()); // notice: it can be 0 length, it's valid.
		asizei loop = 0;
		for(rapidjson::Value::ConstValueIterator mrk = merkles.Begin(); mrk != merkles.End(); ++mrk) {
			if(mrk->IsString() == false) throw std::exception("Merkle array contains non-string element.");
			DecodeHEX(ret->merkles[loop].hash, mrk->GetString());
			loop++;
		}
		return ret.release();
	}
};


}
}
