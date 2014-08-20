/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "Init.h"


Settings* LoadConfig(const wchar_t *filename) {
	using std::unique_ptr;
	Json::Reader reader; // kept here, in case persistent data is required.
	Json::Value root;
	std::ifstream input(filename);
	if(input.bad() || !input.is_open()) throw std::exception("Invalid file to open.");
	reader.parse(input, root, false);
	unique_ptr<Settings> ret(new Settings);

	if(root["pools"].isObject() == false) throw std::exception("Invalid pool specification, array expected.");
	else {
		auto pools = root["pools"];
		Json::Value::Members keys(pools.getMemberNames());
		for(size_t loadIndex = 0; loadIndex < pools.size(); loadIndex++) {
			Json::Value &load(pools[keys[loadIndex]]); // you're an idiot jsoncpp!
			if(!load.isObject()) continue; // is this the right thing to do?
			auto addr = load["url"];
			auto user = load["user"];
			auto psw = load["pass"];
			auto proto = load["protocol"];
			auto coinDiff = load["coinDiffMul"];
			auto merkleMode = load["merkleMode"];
			auto algo = load["algo"];
			bool valid = addr.isString() && user.isString() && psw.isString() && algo.isString();
			if(!valid) continue; //!< \todo signal this issue
			string url = addr.asString();
			string forced = proto.asString();
			if(forced.empty() == false) {
				if(url.find_first_of(forced) != 0) { // not found, so the parsing rules for PoolInfo won't be satisfied
					url = forced + "+" + url;
				}
			}
			unique_ptr<PoolInfo> add(new PoolInfo(keys[loadIndex], url, user.asString(), psw.asString()));
			add->algo = algo.asString();
			if(coinDiff.isNull() == false && (coinDiff.isUInt() || coinDiff.isInt())) {
				if(coinDiff.isUInt()) add->diffOneMul = coinDiff.asUInt();
				else {
					__int64 crap = coinDiff.asInt();
					add->diffOneMul = static_cast<unsigned __int64>(crap);
				}
			}
			if(merkleMode.isString()) {
				if(merkleMode.asString() == "SHA256D") add->merkleMode = PoolInfo::mm_SHA256D;
				else if(merkleMode.asString() == "singleSHA256") add->merkleMode = PoolInfo::mm_singleSHA256;
				else throw std::string("Unknown merkle mode: \"" + merkleMode.asString() + "\".");
			}
			ret->pools.push_back(std::move(add));
		}
		if(!ret->pools.size()) throw std::exception("No valid pool configs found.");
	}
	if(root["driver"].isString()) ret->driver = root["driver"].asString();
	if(root["algo"].isString()) ret->algo = root["algo"].asString();
	if(root["impl"].isString()) ret->impl = root["impl"].asString();
	if(root["checkNonces"].isBool()) ret->checkNonces = root["checkNonces"].asBool();

	if(root["implParams"].isObject()) {
		// This function only checks correspondance.
		auto parseParam = [](const char *name, Settings::ImplParamType type, const Json::Value &value) {
			switch(type) {
			case Settings::ImplParamType::ipt_uint: {
				if(value.isUInt()) return Settings::ImplParam(name, value.asUInt());
				else if(value.isInt() && value.asInt() >= 0) return Settings::ImplParam(name, static_cast<unsigned int>(value.asInt()));
				break;
			}
			}
			throw std::exception("Parameter found, but type not matched.");
		};
		const char **paramNames = nullptr;
		const Settings::ImplParamType *types = nullptr;
		auto newParam = [&parseParam, &paramNames, &types](const std::string &name, const Json::Value &value) {
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
		const Json::Value &p(root["implParams"]);
		Json::Value::Members keys(p.getMemberNames());
		for(auto sub = keys.cbegin(); sub != keys.cend(); ++sub) {
			auto eq = [&sub](const Settings::ImplParam &test) { return test.name == *sub; };
			auto el = std::find_if(ret->implParams.begin(), ret->implParams.end(), eq);
			if(el != ret->implParams.end()) *el = newParam(*sub, p[*sub]); // technically this cannot happen -> JSON would fail to parse
			else ret->implParams.push_back(newParam(*sub, p[*sub]));
		}
	}
	return ret.release();
}
