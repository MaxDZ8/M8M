/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../../MinerInterface.h"
#include "../AbstractCommand.h"

namespace commands {
namespace monitor {

class AlgoCMD : public AbstractCommand {
	MinerInterface &miner;
public:
	AlgoCMD(MinerInterface &worker) : miner(worker), AbstractCommand("algo") { }
	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		// Specification mandates there should be 1 parameter of value "primary".
		// This is not really required (works perfectly with no params at all) but required for being future proof.
		using namespace rapidjson;
		std::string mode("primary");
		Value::ConstMemberIterator params = input.FindMember("params");
		if(params == input.MemberEnd() || params->value.IsNull()) { }
		else if(params->value.IsString()) { mode.assign(params->value.GetString(), params->value.GetStringLength()); }
		else {
			build.SetString("!!ERROR: \"parameters\" specified, but not a valid format.");
			return nullptr;
		}
		if(mode != "primary") {
			std::string msg("!!ERROR: \"parameters\" unrecognized value \"" + mode + "\"");
			build.SetString(msg.c_str(), rapidjson::SizeType(msg.length()), build.GetAllocator());
			return nullptr;
		}
		const char *algo = miner.GetMiningAlgo();
		build.SetObject();
		if(algo == nullptr) return nullptr;
		build.AddMember("algo", StringRef(algo), build.GetAllocator());
		// can be put there directly as miner is managed by same thread, guaranteed lifetime, miner destroyed between passes.
		std::string impl;
		aulong ver;
		char buffer[20]; // 8*2+1 would be sufficient for aulong
		if(miner.GetMiningAlgoImpInfo(impl, ver)) {
			_ui64toa_s(ver, buffer, sizeof(buffer), 16);
			build.AddMember("impl", Value(impl.c_str(), rapidjson::SizeType(impl.length()), build.GetAllocator()), build.GetAllocator());
			build.AddMember("version", Value(buffer, rapidjson::SizeType(strlen(buffer)), build.GetAllocator()), build.GetAllocator());
		}
		return nullptr;
	}
};


}
}
