/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCommand.h"
#include "../../ProcessingNodesFactory.h"

namespace commands {
namespace monitor {

class RejectReasonCMD : public AbstractCommand {
    std::vector< std::vector<MinerSupport::ConfReasons> > rejectReasons;
public:
	RejectReasonCMD(const std::vector< std::vector<MinerSupport::ConfReasons> > &why) : rejectReasons(why), AbstractCommand("rejectReason") { }
	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		using namespace rapidjson;
		build.SetArray();
        build.Reserve(SizeType(rejectReasons.size()), build.GetAllocator());
        for(auto &el : rejectReasons) {
            Value arr(kArrayType);
            arr.Reserve(SizeType(el.size()), build.GetAllocator());
            for(auto &conf : el) {
                if(conf.bad.empty()) continue;
                Value entry(kObjectType);
                entry.AddMember("confIndex", conf.configIndex, build.GetAllocator());
                Value list(kArrayType);
                list.Reserve(SizeType(conf.bad.size()), build.GetAllocator());
                for(auto &reason : conf.bad) {
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
};


}
}
