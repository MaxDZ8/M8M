/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractStreamingCommand.h"
#include <memory>

namespace commands {
namespace monitor {

class DeviceShares : public AbstractStreamingCommand {
public:
	struct ShareStats {
		aulong found, bad, discarded, stale;
        std::chrono::time_point<std::chrono::system_clock> last;
        adouble dsps; //!< this is akin to work utility in legacy miners but not quite!

		ShareStats() : found(0), bad(0), stale(0), discarded(0), dsps(.0) { }
        bool operator!=(const ShareStats &other) const {
            return found != other.found || bad != other.bad || discarded != other.discarded || stale != other.stale || dsps != other.dsps;
        }
	};
	class ValueSourceInterface {
	public:
		virtual ~ValueSourceInterface() { }
		virtual bool GetDeviceShareStats(ShareStats &out, asizei devLinearIndex) = 0;
	};

	DeviceShares(ValueSourceInterface &src) : devices(src), AbstractStreamingCommand("deviceShares") { }


private:
	ValueSourceInterface &devices;

	AbstractInternalPush* NewPusher() { return new Pusher(devices); }
	
	class Pusher : public AbstractInternalPush {
		ValueSourceInterface &devices;
		std::vector<ShareStats> poll;

	public:
		Pusher(ValueSourceInterface &getters) : devices(getters) { }
		bool MyCommand(const std::string &signature) const { return strcmp(signature.c_str(), "deviceShares") == 0; }
		std::string GetPushName() const { return std::string("deviceShares"); }
		void SetState(const rapidjson::Value &input) {
			asizei count = 0; 
			ShareStats out;
			while(devices.GetDeviceShareStats(out, count)) count++;
			poll.resize(count);
		}
		bool RefreshAndReply(rapidjson::Document &build, bool changes) {
			using namespace rapidjson;
			build.SetObject();
            auto mkSizedArr = [&build, this](const char *name) -> Value& {
			    build.AddMember(StringRef(name), Value(kArrayType), build.GetAllocator());
                Value &ret(build[name]);
                ret.Reserve(SizeType(poll.size()), build.GetAllocator());
                return ret;
            };
			Value &found(mkSizedArr("found"));
			Value &bad(mkSizedArr("bad"));
			Value &discarded(mkSizedArr("discarded"));
			Value &stale(mkSizedArr("stale"));
			Value &dsps(mkSizedArr("dsps"));
			Value &lastResult(mkSizedArr("lastResult"));
			for(asizei loop = 0; loop < poll.size(); loop++) {
				ShareStats previously = poll[loop];
				devices.GetDeviceShareStats(poll[loop], loop);
                changes |= previously != poll[loop];
			}
			if(changes) {
				for(asizei loop = 0; loop < poll.size(); loop++) {
					found.PushBack(poll[loop].found, build.GetAllocator());
					bad.PushBack(poll[loop].bad, build.GetAllocator());
					discarded.PushBack(poll[loop].discarded, build.GetAllocator());
					stale.PushBack(poll[loop].stale, build.GetAllocator());
					dsps.PushBack(poll[loop].dsps, build.GetAllocator());
                    auto sinceEpochLast = std::chrono::duration_cast<std::chrono::seconds>(poll[loop].last.time_since_epoch());
					lastResult.PushBack(sinceEpochLast.count(), build.GetAllocator());
				}
			}
			return changes;
		}
	};
};


}
}
