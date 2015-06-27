/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCommand.h"

namespace commands {
namespace monitor {

class DeviceConfigCMD : public AbstractCommand {
public:
    struct DeviceConfigMapperInterface {
        virtual ~DeviceConfigMapperInterface() { }
        virtual std::vector<auint> GetConfigMappings() const = 0; //!< device i uses config [i], auint(-1) if off.
    };
	DeviceConfigCMD(DeviceConfigMapperInterface &allDevices) : mapper(allDevices), AbstractCommand("deviceConfig") { }
	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		// In theory this could take various forms such as enumerating only specific devices...
		// But I really don't care. For the time being I just map'em all!
		build.SetArray();
        auto mapping(mapper.GetConfigMappings());
        build.Reserve(rapidjson::SizeType(mapping.size()), build.GetAllocator());
        for(auto index : mapping) {
            if(index == auint(-1)) build.PushBack(rapidjson::Value("off"), build.GetAllocator());
            else build.PushBack(rapidjson::Value(index), build.GetAllocator());
        }
		return nullptr;
	}
private:
    DeviceConfigMapperInterface &mapper;
};


}
}
