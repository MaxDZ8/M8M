/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCommand.h"

namespace commands {
namespace monitor {

class RejectReasonCMD : public AbstractCommand {
public:
    struct RejInfoProviderInterface {
        virtual ~RejInfoProviderInterface() { }
        virtual asizei GetNumEntries() const = 0; //!< basically number of devices, each device an array of possible reject reasons
        virtual asizei GetNumRejectedConfigs(asizei dev) const = 0; //!< number of failing configs for device dev
        virtual std::vector<std::string> GetRejectionReasons(asizei dev, asizei entry) const = 0; //!< list of rejection description strings
        virtual auint GetRejectedConfigIndex(asizei dev, asizei entry) const = 0; //!< config index attempted 
    };

	RejectReasonCMD(RejInfoProviderInterface &why) : rejectReasons(why), AbstractCommand("rejectReason") { }
	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		using namespace rapidjson;
		build.SetArray();
        build.Reserve(SizeType(rejectReasons.GetNumEntries()), build.GetAllocator());
        for(asizei loop = 0; loop < rejectReasons.GetNumEntries(); loop++) {
            Value arr(kArrayType);
            arr.Reserve(SizeType(rejectReasons.GetNumRejectedConfigs(loop)), build.GetAllocator());
            for(asizei inner = 0; inner < rejectReasons.GetNumRejectedConfigs(loop); inner++) {
                auto bad(rejectReasons.GetRejectionReasons(loop, inner));
                if(bad.empty()) continue;
                Value entry(kObjectType);
                entry.AddMember("confIndex", rejectReasons.GetRejectedConfigIndex(loop, inner), build.GetAllocator());
                Value list(kArrayType);
                list.Reserve(SizeType(bad.size()), build.GetAllocator());
                for(auto &reason : bad) {
                    Value string(reason.c_str(), SizeType(reason.length()), build.GetAllocator());
                    list.PushBack(string, build.GetAllocator());
                }
                entry.AddMember("reasons", list, build.GetAllocator());
                arr.PushBack(entry, build.GetAllocator());
            }
            build.PushBack(arr, build.GetAllocator());

        }
		return nullptr;
	}
private:
    RejInfoProviderInterface &rejectReasons;
};


}
}
