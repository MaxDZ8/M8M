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

class DeviceConfigCMD : public AbstractCommand {
	MinerInterface &miner;
public:
	DeviceConfigCMD(MinerInterface &worker) : miner(worker), AbstractCommand("deviceConfig?") { }
	PushInterface* Parse(Json::Value &build, const Json::Value &input) {
		// In theory this could take various forms such as enumerating only specific devices...
		// But I really don't care. For the time being I just map'em all!
		build = Json::Value(Json::arrayValue); // Can this be an array directly?
		asizei index, device = 0;
		while(miner.GetDeviceConfig(index, device)) {
			if(!index) build[device] = "off";
			else build[device] = index - 1;
			device++;
		}
		return nullptr;
	}
};


}
}
