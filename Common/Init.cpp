/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "Init.h"


Settings* LoadConfig(const wchar_t *filename) {
	using std::unique_ptr;
	using namespace rapidjson;
	FILE *jsonFile = nullptr;
	if(_wfopen_s(&jsonFile, filename, L"rb")) throw std::exception("Cannot open config file.");
	ScopedFuncCall autoClose([jsonFile]() { fclose(jsonFile); });
	char jsonReadBuffer[512];
	FileReadStream jsonIN(jsonFile, jsonReadBuffer, sizeof(jsonReadBuffer));
	AutoUTFInputStream<unsigned __int32, FileReadStream> input(jsonIN);
	Document root; // a Document is a Value in rapidjson, with some additional things
	root.ParseStream< 0, AutoUTF<unsigned> >(input);
	if(root.HasParseError()) {
		const ParseErrorCode error = root.GetParseError();
		throw std::string("Invalid json, parse failed with error ") + GetParseError_En(error) + " @ " + std::to_string(root.GetErrorOffset());
	}
	unique_ptr<Settings> ret(new Settings);

	if(root.IsObject() == false) throw std::exception("Not a valid configuration file.");

	// rapidjson strings are pointers (coherently with the "no copy" policy, they also might contain the unicode null character.
	// rapidjson mangles them correctly but they don't make any sense at all for me and they very likely don't behave as user intended...
	// ... who would use null anyway? Looks like someone might be trying to mess up us.
	auto mkString = [](const Value &jv) { return std::string(jv.GetString(), jv.GetStringLength()); };
	auto condAssign = [](std::string &dst, const Value &src) { if(src.IsString()) dst.assign(src.GetString(), src.GetStringLength()); };

	Value::MemberIterator pools = root.FindMember("pools");
	if(pools == root.MemberEnd()) throw std::exception("No pools specified in config file.");
	else if(pools->value.IsObject() == false) throw std::exception("Pool list must be an object.");
	else {
		for(Value::ConstMemberIterator keys = pools->value.MemberBegin(); keys != pools->value.MemberEnd(); ++keys) {
			const Value &load(keys->value);
			if(!load.IsObject()) continue; // is this the right thing to do?
			const auto addr(load.FindMember("url"));
			const auto user(load.FindMember("user"));
			const auto psw(load.FindMember("pass"));
			const auto proto(load.FindMember("protocol"));
			const auto coinDiff(load.FindMember("coinDiffMul"));
			const auto merkleMode(load.FindMember("merkleMode"));
			const auto algo(load.FindMember("algo"));
			bool valid = addr != load.MemberEnd() && addr->value.IsString();
			valid &= user != load.MemberEnd() && user->value.IsString();
			valid &= psw != load.MemberEnd() && psw->value.IsString();
			valid &= algo != load.MemberEnd() && algo->value.IsString();
			valid &= user != load.MemberEnd() && user->value.IsString();
			valid &= psw != load.MemberEnd() && psw->value.IsString();
			if(!valid) continue; //!< \todo signal this issue
			string url = mkString(addr->value);
			string forced = proto == load.MemberEnd()? "" : mkString(proto->value);
			if(forced.empty() == false) {
				if(url.find_first_of(forced) != 0) { // not found, so the parsing rules for PoolInfo won't be satisfied
					url = forced + "+" + url;
				}
			}
			unique_ptr<PoolInfo> add(new PoolInfo(mkString(keys->name), url, mkString(user->value), mkString(psw->value)));
			if(algo != load.MemberEnd()) condAssign(add->algo, algo->value);
			if(coinDiff != load.MemberEnd() && (coinDiff->value.IsUint() || coinDiff->value.IsInt())) {
				if(coinDiff->value.IsUint()) add->diffOneMul = coinDiff->value.GetUint();
				else {
					__int64 crap = coinDiff->value.GetInt();
					add->diffOneMul = static_cast<unsigned __int64>(crap);
				}
			}
			if(merkleMode != load.MemberEnd() && merkleMode->value.IsString()) {
				std::string mmode(merkleMode->value.GetString(), merkleMode->value.GetStringLength());
				if(mmode == "SHA256D") add->merkleMode = PoolInfo::mm_SHA256D;
				else if(mmode == "singleSHA256") add->merkleMode = PoolInfo::mm_singleSHA256;
				else throw std::string("Unknown merkle mode: \"" + mmode + "\".");
			}
			ret->pools.push_back(std::move(add));
		}
		if(!ret->pools.size()) throw std::exception("No valid pool configs found.");
	}
	Value::ConstMemberIterator driver = root.FindMember("driver");
	Value::ConstMemberIterator algo = root.FindMember("algo");
	Value::ConstMemberIterator impl = root.FindMember("impl");
	if(driver != root.MemberEnd()) condAssign(ret->driver, driver->value);
	if(algo != root.MemberEnd()) condAssign(ret->algo, algo->value);
	if(impl != root.MemberEnd()) condAssign(ret->impl, impl->value);
	Value::ConstMemberIterator checkNonces = root.FindMember("checkNonces");
	if(checkNonces != root.MemberEnd() && checkNonces->value.IsBool()) ret->checkNonces = checkNonces->value.GetBool();

	Value::ConstMemberIterator implParams = root.FindMember("implParams");
	if(implParams != root.MemberEnd() && implParams->value.IsObject()) {
		// This function only checks correspondance.
		auto parseParam = [](const char *name, Settings::ImplParamType type, const Value &value) {
			switch(type) {
			case Settings::ImplParamType::ipt_uint: {
				if(value.IsUint()) return Settings::ImplParam(name, value.GetUint());
				else if(value.IsInt() && value.GetInt() >= 0) return Settings::ImplParam(name, static_cast<unsigned int>(value.GetInt()));
				break;
			}
			}
			throw std::exception("Parameter found, but type not matched.");
		};
		const char **paramNames = nullptr;
		const Settings::ImplParamType *types = nullptr;
		auto newParam = [&parseParam, &paramNames, &types](const std::string &name, const Value &value) {
			for(size_t loop = 0; paramNames[loop]; loop++) {
				if(name == paramNames[loop]) return parseParam(paramNames[loop], types[loop], value);
			}
			throw std::exception("Implementation parameter not matched.");
		};
		auto UINT = Settings::ipt_uint;
		const char *scrypt1024_monolithic_names[] = { "worksize", "linearIntensity", "step2e", "dispatchCount", nullptr };
		const Settings::ImplParamType scrypt1024_monolithic_types[] = { UINT, UINT, UINT, UINT };

		const char *scrypt1024_multiStep_names[] = { "worksize", "linearIntensity", "step2e", "dispatchCount", nullptr };
		const Settings::ImplParamType scrypt1024_multiStep_types[] = { UINT, UINT, UINT, UINT };

		const char *qubit_multiStep_names[] = { "linearIntensity", "dispatchCount" };
		const Settings::ImplParamType qubit_multiStep_types[] = { UINT, UINT };

		if(ret->algo == "scrypt1024") {
			if(!_stricmp(ret->impl.c_str(), "multiStep")) {
				paramNames = scrypt1024_multiStep_names;
				types = scrypt1024_multiStep_types;					
			}
			else throw std::exception("Unrecognized scrypt1024 implementation.");
		}
		else if(ret->algo == "qubit") {
			if(!_stricmp(ret->impl.c_str(), "fiveSteps")) {
				//! \todo this really sucks. The algorithms themselves should be collaborating here, their parsers should be able
				//! to get there and mangle everything accordingly so there would be no need for this huge pile of crap and redundancy.
				paramNames = qubit_multiStep_names;
				types = qubit_multiStep_types;					
			}
			else throw std::exception("Unrecognized qubit implementation.");
		}
		for(Value::ConstMemberIterator sub = implParams->value.MemberBegin(); sub != implParams->value.MemberEnd(); ++sub) {
			auto eq = [&sub](const Settings::ImplParam &test) { return test.name == std::string(sub->name.GetString(), sub->name.GetStringLength()); };
			auto el = std::find_if(ret->implParams.begin(), ret->implParams.end(), eq);
			if(el != ret->implParams.end()) *el = newParam(std::string(sub->name.GetString(), sub->name.GetStringLength()), sub->value);
			else ret->implParams.push_back(newParam(std::string(sub->name.GetString(), sub->name.GetStringLength()), sub->value));
		}
	}
	return ret.release();
}
