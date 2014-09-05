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

class RejectReasonCMD : public AbstractCommand {
	MinerInterface &miner;
public:
	RejectReasonCMD(MinerInterface &worker) : miner(worker), AbstractCommand("rejectReason") { }
	PushInterface* Parse(Json::Value &build, const Json::Value &input) {
		build = Json::Value(Json::arrayValue);
		asizei index, device = 0;
		while(miner.GetDeviceConfig(index, device)) {
			if(index) build[device] = Json::Value(Json::nullValue);
			else {
				build[device] = Json::Value(Json::arrayValue);
				Json::Value &reason(build[device]);
				std::vector<std::string> algoReason(miner.GetBadConfigReasons(device));
				for(asizei loop = 0; loop < algoReason.size(); loop++) reason[loop] = algoReason[loop];
			}
			device++;
		}
		return nullptr;
	}
};


}
}
