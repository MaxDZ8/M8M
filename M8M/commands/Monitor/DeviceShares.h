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
		aulong good, bad, stale;
		time_t lastResult;
		ShareStats() : good(0), bad(0), stale(0), lastResult(0) { }
		bool operator==(const ShareStats &other) const {
			return other.good == good && other.bad == bad && other.stale == stale;
		}
		bool operator!=(const ShareStats &other) const { return !(*this == other); }
	};
	class ValueSourceInterface {
	public:
		virtual ~ValueSourceInterface() { }
		virtual bool GetDSS(ShareStats &out, asizei devLinearIndex) = 0;
	};

	DeviceShares(ValueSourceInterface &src) : devices(src), AbstractStreamingCommand("deviceShares") { }


private:
	ValueSourceInterface &devices;

	AbstractInternalPush* NewPusher() { return new Pusher(devices); }
	
	class Pusher : public AbstractInternalPush {
		ValueSourceInterface &devices;
		std::vector<ShareStats> poll;
		bool isRefresh;

	public:
		Pusher(ValueSourceInterface &getters) : devices(getters), isRefresh(false) { }
		bool MyCommand(const std::string &signature) const { return strcmp(signature.c_str(), "deviceShares") == 0; }
		std::string GetPushName() const { return std::string("deviceShares"); }
		void SetState(const rapidjson::Value &input) {
			asizei count = 0; 
			ShareStats out;
			while(devices.GetDSS(out, count)) count++;
			poll.resize(count);
		}
		bool RefreshAndReply(rapidjson::Document &build, bool changes) {
			using namespace rapidjson;
			build.SetObject();
			build.AddMember("good", Value(kArrayType), build.GetAllocator());
			build.AddMember("bad", Value(kArrayType), build.GetAllocator());
			build.AddMember("stale", Value(kArrayType), build.GetAllocator());
			build.AddMember("lastResult", Value(kArrayType), build.GetAllocator());
			Value &good(build["good"]);
			Value &bad(build["bad"]);
			Value &stale(build["stale"]);
			Value &lastResult(build["lastResult"]);
			for(asizei loop = 0; loop < poll.size(); loop++) {
				ShareStats previously = poll[loop];
				devices.GetDSS(poll[loop], loop);
				if(previously == poll[loop] && isRefresh) continue;
				changes = true;
			}
			if(changes) {
				for(asizei loop = 0; loop < poll.size(); loop++) {
					bad.PushBack(poll[loop].bad, build.GetAllocator());
					good.PushBack(poll[loop].good, build.GetAllocator());
					stale.PushBack(poll[loop].stale, build.GetAllocator());
					lastResult.PushBack(poll[loop].lastResult, build.GetAllocator());
				}
			}
			isRefresh = true;
			return changes;
		}
	};
};


}
}
