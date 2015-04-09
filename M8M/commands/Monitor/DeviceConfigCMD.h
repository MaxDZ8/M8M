/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCommand.h"

namespace commands {
namespace monitor {

class DeviceConfigCMD : public AbstractCommand {
    std::vector<auint> mapping; //!< device i uses config [i], auint(-1) if off.
public:
	DeviceConfigCMD(const std::vector<auint> &allDevices) : mapping(allDevices), AbstractCommand("deviceConfig") { }
	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		// In theory this could take various forms such as enumerating only specific devices...
		// But I really don't care. For the time being I just map'em all!
		build.SetArray();
        build.Reserve(rapidjson::SizeType(mapping.size()), build.GetAllocator());
        for(auto index : mapping) {
            if(index == auint(-1)) build.PushBack(rapidjson::Value("off"), build.GetAllocator());
            else build.PushBack(rapidjson::Value(index), build.GetAllocator());
        }
		return nullptr;
	}
};


}
}
