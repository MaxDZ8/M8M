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
	AlgoCMD(MinerInterface &worker) : miner(worker), AbstractCommand("algo?") { }
	PushInterface* Parse(Json::Value &build, const Json::Value &input) {
		// Easy, as all params are ignored.
		build = Json::Value(Json::objectValue);
		const char *algo = miner.GetMiningAlgo();
		if(algo) build["algo"] = algo;
		std::string impl, ver;
		if(miner.GetMiningAlgoImpInfo(impl, ver)) {
			build["impl"] = impl;
			build["version"] = ver;
		}
		return nullptr;
	}
};


}
}
