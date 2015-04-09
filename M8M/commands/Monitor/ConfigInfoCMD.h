/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../../AbstractAlgorithm.h"
#include "../AbstractCommand.h"


namespace commands {
namespace monitor {

class ConfigInfoCMD : public AbstractCommand {
public:
    struct ConfigInfo {
        const rapidjson::Value *specified = nullptr; //!< original json value pulled from config file
        std::vector<std::string> rejectReasons; //!< device-independant reasons for which the object was discarded.

        // Stuff below is only valid when rejectReasons.empty() is true. 

        std::vector<auint> devices; //!< might be from different context/platforms. Linear indices.
    };


    struct ConfigDesc {
        bool selected = false; //!< true if {algo,impl} pair selects a valid algo
        bool specified = false; //!< true if a valid config object has been pulled from config file
        // if object has been specified, here are the configurations in the order they appear in config
        std::vector<ConfigInfo> configs; //!< if this is non-empty the [i] configuration object has been considered invalid.
        std::vector<AbstractAlgorithm::ConfigDesc> informative; //!< config for each device, in linear index order, valid indices contained in configs
    };

    const ConfigDesc conf;

	ConfigInfoCMD(const ConfigDesc &desc) : conf(desc), AbstractCommand("configInfo") { }
	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		// .params is an array of strings, static informations to be retrieved
		using namespace rapidjson;
        if(conf.selected == false) {
            build.SetNull();
            return nullptr;
        }
        build.SetArray();
        if(conf.specified == false) return nullptr;
        build.Reserve(SizeType(conf.configs.size()), build.GetAllocator());
        for(auto &el : conf.configs) {
            Value entry;
            if(el.rejectReasons.size()) {
                Value reasons(kArrayType);
                for(auto &str : el.rejectReasons) reasons.PushBack(StringRef(str.c_str()), build.GetAllocator());
                entry.SetObject();
                entry.AddMember("rejectReasons", reasons, build.GetAllocator());
                Document copy;
                copy.CopyFrom(*el.specified, build.GetAllocator());
                entry.AddMember("settings", copy, build.GetAllocator());
            }
            else {
                entry.SetArray();
                for(auto &dev : el.devices) {
                    Value spec(kObjectType);
                    spec.AddMember("device", dev, build.GetAllocator());
                    spec.AddMember("hashCount", conf.informative[dev].hashCount, build.GetAllocator());
                    spec.AddMember("memUsage", Describe(conf.informative[dev].memUsage, build.GetAllocator()), build.GetAllocator());
                    entry.PushBack(spec, build.GetAllocator());
                }
            }
            build.PushBack(entry, build.GetAllocator());
        }
		return nullptr;
	}

private:
	static rapidjson::Value Describe(const std::vector<AbstractAlgorithm::ConfigDesc::MemDesc> &resources, rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator> &alloc) {
		using namespace rapidjson;
        Value arr(kArrayType);
        arr.Reserve(SizeType(resources.size()), alloc);
        for(auto &res : resources) {
            Value entry(kObjectType);
            entry.AddMember("space", StringRef(res.memoryType == AbstractAlgorithm::ConfigDesc::as_device? "device" : "host"), alloc);
            entry.AddMember("presentation", StringRef(res.presentation.c_str()), alloc);
            entry.AddMember("footprint", res.bytes, alloc);
            entry.AddMember("notes", Value(kArrayType), alloc); // not implemented yet
            arr.PushBack(entry, alloc);
        }
        return arr;
	}
};


}
}
