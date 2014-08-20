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

class ConfigInfoCMD : public AbstractCommand {
	const MinerInterface &miner;

	void FillJSON(Json::Value &dst, AlgoImplementationInterface::SettingsInfo::BufferInfo &res) {
		dst["space"] = res.addressSpace;
		dst["presentation"] = res.presentation;
		dst["footprint"] = res.footprint;
		dst["accessType"] = Json::Value(Json::arrayValue);
		Json::Value &qual(dst["accessType"]);
		for(asizei cp = 0; cp < res.accessType.size(); cp++) qual[cp] = res.accessType[cp];
	}

public:
	ConfigInfoCMD(MinerInterface &shared) : miner(shared), AbstractCommand("configInfo?") { }
	PushInterface* Parse(Json::Value &build, const Json::Value &input) {
		// .params is an array of strings, static informations to be retrieved
		const Json::Value &params(input["params"]);
		if(params.isArray() == false) throw std::string("Parameter array is missing.");
		bool hashCount = false;
		bool memUsage = false;
		for(asizei loop = 0; loop < params.size(); loop++) {
			if(params[loop].isString() == false) continue;
			if(!strcmp("hashCount", params[loop].asCString())) hashCount = true;
			else if(!strcmp("memUsage", params[loop].asCString())) memUsage = true;
		}
		std::string impl, version;
		const char *algo = miner.GetMiningAlgo();
		miner.GetMiningAlgoImpInfo(impl, version);
		const AlgoImplementationInterface *ai = miner.GetAI(algo, impl.c_str());
		if(!ai) return nullptr;
		build = Json::Value(Json::arrayValue);
		for(asizei loop = 0; loop < ai->GetNumSettings(); loop++) {
			AlgoImplementationInterface::SettingsInfo info;
			ai->GetSettingsInfo(info, loop);
			build[loop] = Json::Value(Json::objectValue);
			Json::Value &entry(build[loop]);
			if(hashCount) entry["hashCount"] = Json::Value(info.hashCount);
			if(memUsage) {
				entry["resources"] = Json::Value(Json::arrayValue);
				Json::Value &fill(entry["resources"]);
				for(asizei inner = 0; inner < info.resource.size(); inner++) {
					fill[inner] = Json::Value(Json::objectValue);
					FillJSON(fill[inner], info.resource[inner]);
				}
			}
		}
		return nullptr;
	}
};


}
}
