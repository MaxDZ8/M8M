/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../../MinerInterface.h"
#include "../AbstractCommand.h"

namespace commands {
namespace monitor {

class DeviceConfigCMD : public AbstractCommand {
	MinerInterface &miner;
public:
	DeviceConfigCMD(MinerInterface &worker) : miner(worker), AbstractCommand("deviceConfig") { }
	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		// In theory this could take various forms such as enumerating only specific devices...
		// But I really don't care. For the time being I just map'em all!
		build.SetArray();
		asizei index, device = 0;
		while(miner.GetDeviceConfig(index, device)) {
			if(!index) build.PushBack(rapidjson::Value("off"), build.GetAllocator());
			else build.PushBack(index - 1, build.GetAllocator());
			device++;
		}
		return nullptr;
	}
};


}
}
