/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../../MinerInterface.h"
#include "../AbstractCommand.h"

namespace commands {
namespace monitor {

class RejectReasonCMD : public AbstractCommand {
	MinerInterface &miner;
public:
	RejectReasonCMD(MinerInterface &worker) : miner(worker), AbstractCommand("rejectReason") { }
	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		using namespace rapidjson;
		build.SetArray();
		asizei index, device = 0;
		while(miner.GetDeviceConfig(index, device)) {
			if(index) build.PushBack(Value(kNullType), build.GetAllocator());
			else {
				build.PushBack(Value(kArrayType), build.GetAllocator());
				Value &reason(build[device]);
				std::vector<std::string> algoReason(miner.GetBadConfigReasons(device));
				reason.Reserve(algoReason.size(), build.GetAllocator());
				for(asizei loop = 0; loop < algoReason.size(); loop++)
					reason.PushBack(Value(algoReason[loop].c_str(), algoReason[loop].length(), build.GetAllocator()), build.GetAllocator());
			}
			device++;
		}
		return nullptr;
	}
};


}
}
