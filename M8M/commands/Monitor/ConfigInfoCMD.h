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
        std::string impl; //!< algorithm implementation being used
        std::vector<auint> devices; //!< might be from different context/platforms. Linear indices.
    };

    struct ConfigDescriptorInterface {
        virtual ~ConfigDescriptorInterface() { }
        virtual bool ValidSelection() const = 0; //!< true if {algo,impl} pair selected a valid algo
        virtual std::string GetSelectedAlgorithm() const = 0;
        virtual bool ValidConfig() const = 0; //!< true if a corresponding set of configs has been successfully loaded from config file
        virtual asizei GetNumDeducedConfigs() const = 0; //!< how many configs to probe using GetConfig or GetResources
        virtual ConfigInfo GetConfig(asizei cfg) const = 0; //!< every used device is mapped to a config and therefore to a single set of resources
        /*! This is a bit more complicated. GetConfig will return a list of devices identified by linear index using the requested config.
        Just iterate on all linear indices pulled from that array to get a descriptor for each device. Even if all devices use the same config,
        they can still have different resources (in the future when "elastic" intensity will see the light).
        You can still probe an unused device and it will pass out a ConfigDesc with hashCout 0 returning true. */
        virtual bool GetResources(AbstractAlgorithm::ConfigDesc &desc, auint dev) const = 0;
    };

	ConfigInfoCMD(const ConfigDescriptorInterface &desc) : conf(desc), AbstractCommand("configInfo") { }
	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		// .params is an array of strings, static informations to be retrieved
		using namespace rapidjson;
        if(conf.ValidSelection() == false) {
            build.SetNull();
            return nullptr;
        }
        auto &alloc(build.GetAllocator());
        auto selAlgo(conf.GetSelectedAlgorithm());
        build.SetObject();
        build.AddMember("algo", Value(selAlgo.c_str(), SizeType(selAlgo.length()), alloc), alloc);
        Value confArr(kNullType);
        if(conf.ValidConfig()) {
            confArr.SetArray();
            confArr.Reserve(SizeType(conf.GetNumDeducedConfigs()), alloc);
            for(asizei loop = 0; loop < conf.GetNumDeducedConfigs(); loop++) {
                auto el(conf.GetConfig(loop));
                Value entry;
                if(el.rejectReasons.size()) {
                    Value reasons(kArrayType);
                    for(auto &str : el.rejectReasons) reasons.PushBack(StringRef(str.c_str()), alloc);
                    entry.SetObject();
                    entry.AddMember("rejectReasons", reasons, alloc);
                    Document copy;
                    copy.CopyFrom(*el.specified, alloc);
                    entry.AddMember("settings", copy, alloc);
                }
                else {
                    entry.SetObject();
                    entry.AddMember("impl", Value(el.impl.c_str(), SizeType(el.impl.length()), alloc), alloc);
                    Value devArr(kArrayType);
                    for(auto &dev : el.devices) {
                        Value spec(kObjectType);
                        AbstractAlgorithm::ConfigDesc info;
                        conf.GetResources(info, dev);
                        spec.AddMember("device", dev, alloc);
                        spec.AddMember("hashCount", info.hashCount, alloc);
                        spec.AddMember("memUsage", Describe(info.memUsage, alloc), alloc);
                        devArr.PushBack(spec, alloc);
                    }
                    entry.AddMember("active", devArr, alloc);
                }
                confArr.PushBack(entry, alloc);
            }
        }
        build.AddMember("selected", confArr, alloc);
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
    const ConfigDescriptorInterface &conf;
};


}
}
