/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../../MinerInterface.h"
#include "../AbstractCommand.h"


namespace commands {
namespace monitor {

class ConfigInfoCMD : public AbstractCommand {
	const MinerInterface &miner;

	void FillJSON(rapidjson::Value &dst, AlgoImplementationInterface::SettingsInfo::BufferInfo &res, rapidjson::Document &doc) {
		using namespace rapidjson;
		dst.AddMember("space", Value(res.addressSpace.c_str(), res.addressSpace.length(), doc.GetAllocator()), doc.GetAllocator());
		dst.AddMember("presentation", Value(res.presentation.c_str(), res.presentation.length(), doc.GetAllocator()), doc.GetAllocator());
		dst.AddMember("footprint", Value(res.footprint), doc.GetAllocator());
		dst.AddMember("accessType", Value(rapidjson::kArrayType), doc.GetAllocator());
		rapidjson::Value &qual(dst["accessType"]);
		for(asizei cp = 0; cp < res.accessType.size(); cp++) {
			qual.PushBack(Value(res.accessType[cp].c_str(), res.accessType[cp].length(), doc.GetAllocator()), doc.GetAllocator());
		}
	}

public:
	ConfigInfoCMD(MinerInterface &shared) : miner(shared), AbstractCommand("configInfo") { }
	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		// .params is an array of strings, static informations to be retrieved
		using namespace rapidjson;
		Value::ConstMemberIterator &params(input.FindMember("params"));
		if(params == input.MemberEnd() || params->value.IsArray() == false) throw std::string("Parameter array is missing.");
		bool hashCount = false;
		bool memUsage = false;
		for(SizeType loop = 0; loop < params->value.Size(); loop++) {
			if(params->value[loop].IsString() == false) continue;
			const std::string test(params->value[loop].GetString(), params->value[loop].GetStringLength());
			if(test == "hashCount") hashCount = true;
			else if(test == "memUsage") memUsage = true;
		}
		std::string impl;
		aulong version;
		const char *algo = miner.GetMiningAlgo();
		miner.GetMiningAlgoImpInfo(impl, version);
		const AlgoImplementationInterface *ai = algo? miner.GetAI(algo, impl.c_str()) : nullptr;
		build.SetArray();
		if(!ai) return nullptr;
		for(asizei loop = 0; loop < ai->GetNumSettings(); loop++) {
			AlgoImplementationInterface::SettingsInfo info;
			if(ai) ai->GetSettingsInfo(info, loop);
			Value entry(kObjectType);
			if(hashCount) entry.AddMember("hashCount", info.hashCount, build.GetAllocator());
			if(memUsage) {
				entry.AddMember("resources", Value(kArrayType), build.GetAllocator());
				Value &fill(entry["resources"]);
				for(asizei inner = 0; inner < info.resource.size(); inner++) {
					fill.PushBack(Value(kObjectType), build.GetAllocator());
					FillJSON(fill[inner], info.resource[inner], build);
				}
			}
			build.PushBack(entry, build.GetAllocator());
		}
		return nullptr;
	}
};


}
}
