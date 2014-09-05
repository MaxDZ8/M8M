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
		void SetState(const Json::Value &input) {
			asizei count = 0; 
			ShareStats out;
			while(devices.GetDSS(out, count)) count++;
			poll.resize(count);
		}
		bool RefreshAndReply(Json::Value &build, bool changes) {
			auto clamp = [](aulong value) -> auint { 
				aulong biggest(aulong(auint(~0)));
				return auint(value < biggest? value : biggest);
			};
			Json::Value &good(build["good"]);
			Json::Value &bad(build["bad"]);
			Json::Value &stale(build["stale"]);
			Json::Value &lastResult(build["lastResult"]);
			for(asizei loop = 0; loop < poll.size(); loop++) {
				ShareStats previously = poll[loop];
				devices.GetDSS(poll[loop], loop);
				if(previously == poll[loop] && isRefresh) continue;
				changes = true;
			}
			if(changes) {
				for(asizei loop = 0; loop < poll.size(); loop++) {
					bad[bad.size()] = clamp(poll[loop].bad);
					good[good.size()] = clamp(poll[loop].good);
					stale[stale.size()] = clamp(poll[loop].stale);
					lastResult[lastResult.size()] = clamp(poll[loop].lastResult);
				}
			}
			isRefresh = true;
			return changes;
		}
	};
};


}
}
