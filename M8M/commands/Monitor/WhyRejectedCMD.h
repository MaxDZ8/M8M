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

class WhyRejectedCMD : public AbstractCommand {
	MinerInterface &miner;
public:
	WhyRejectedCMD(MinerInterface &worker) : miner(worker), AbstractCommand("whyRejected?") { }
	PushInterface* Parse(Json::Value &build, const Json::Value &input) {
		// In theory this could take various forms such as enumerating only specific devices...
		// But I really don't care. For the time being I just map'em all!
		build = Json::Value(Json::objectValue); // Can this be an array directly?
		build["algo"] = Json::Value(Json::arrayValue);
		Json::Value &algo(build["algo"]);
		asizei index, device = 0;
		while(miner.GetDeviceConfig(index, device)) {
			if(index) algo[device] = Json::Value(Json::nullValue);
			else {
				algo[device] = Json::Value(Json::arrayValue);
				Json::Value &reason(algo[device]);
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
