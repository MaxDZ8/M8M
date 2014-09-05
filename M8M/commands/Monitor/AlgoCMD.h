/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <json/json.h>
#include "../../MinerInterface.h"
#include "../AbstractCommand.h"

namespace commands {
namespace monitor {

class AlgoCMD : public AbstractCommand {
	MinerInterface &miner;
public:
	AlgoCMD(MinerInterface &worker) : miner(worker), AbstractCommand("algo") { }
	PushInterface* Parse(Json::Value &build, const Json::Value &input) {
		// Specification mandates there should be 1 parameter of value "primary".
		// This is not really required (works perfectly with no params at all) but required for being future proof.
		std::string mode("primary");
		if(input["params"].isNull()) { }
		else if(input["params"].isString()) { mode = input["params"].asString(); }
		else {
			build = "!!ERROR: \"parameters\" specified, but not a valid format.";
			return nullptr;
		}
		if(mode != "primary") {
			build = "!!ERROR: \"parameters\" unrecognized value \"" + mode + "\"";
			return nullptr;
		}
		const char *algo = miner.GetMiningAlgo();
		if(algo == nullptr) {
			build = Json::Value(Json::nullValue);
			return nullptr;
		}
		build["algo"] = algo;
		std::string impl;
		aulong ver;
		char buffer[20]; // 8*2+1 would be sufficient for aulong
		if(miner.GetMiningAlgoImpInfo(impl, ver)) {
			_ui64toa_s(ver, buffer, sizeof(buffer), 16);
			build["impl"] = impl;
			build["version"] = buffer;
		}
		return nullptr;
	}
};


}
}
