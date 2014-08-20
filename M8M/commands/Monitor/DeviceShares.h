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
		ShareStats() : good(0), bad(0), stale(0) { }
	};
	class ValueSourceInterface {
	public:
		virtual ~ValueSourceInterface() { }
		virtual bool GetDSS(ShareStats &out, asizei devLinearIndex) = 0;
	};

	DeviceShares(ValueSourceInterface &src) : devices(src), AbstractStreamingCommand("deviceShares?") { }


private:
	ValueSourceInterface &devices;

	AbstractInternalPush* NewPusher() { return new Pusher(devices); }

	struct TrackedStats : public ShareStats {
		asizei linearIndex;
		bool operator==(const TrackedStats &other) const {
			// mempcy would also suffice but not logically correct operation in C++
			return other.linearIndex == linearIndex && other.good == good && other.bad == bad && other.stale == stale;
		}
		bool operator!=(const TrackedStats &other) const { return !(*this == other); }
	};
	
	class Pusher : public AbstractInternalPush {
		ValueSourceInterface &devices;
		std::vector<TrackedStats> poll;
	public:
		Pusher(ValueSourceInterface &getters) : devices(getters) { }
		bool MyCommand(const std::string &signature) const { return strcmp(signature.c_str(), "deviceShares!") == 0; }
		std::string GetPushName() const { return std::string("deviceShares!"); }
		ReplyAction SetState(std::string &error, const Json::Value &input) {
			const Json::Value &index(input["devices"]);
			if(index.isArray() == false) { error = "\"devices\" must be an array";    return ra_bad; }
			asizei numDevices = index.size();
			if(numDevices == 0) return ra_delete;
			for(asizei test = 0; test < numDevices; test++) {
				if(index[test].isConvertibleTo(Json::uintValue) == false) {
					error = "\"devices[" + std::to_string(test) + "]\" is not a valid index";
					return ra_bad;
				}
			}
			poll.resize(numDevices);
			for(asizei loop = 0; loop < numDevices; loop++) poll[loop].linearIndex = index[loop].asUInt();
			return ra_consumed;
		}
		bool RefreshAndReply(Json::Value &build, bool changes) {
			auto clamp = [](aulong value) -> auint { 
				aulong biggest(aulong(auint(~0)));
				return auint(value < biggest? value : biggest);
			};
			build["good"] = Json::Value(Json::arrayValue);
			build["bad"] = Json::Value(Json::arrayValue);
			build["stale"] = Json::Value(Json::arrayValue);
			build["linearIndex"] = Json::Value(Json::arrayValue);
			Json::Value &good(build["good"]);
			Json::Value &bad(build["bad"]);
			Json::Value &stale(build["stale"]);
			Json::Value &linearIndex(build["linearIndex"]);
			TrackedStats zero;
			for(asizei loop = 0; loop < poll.size(); loop++) {
				TrackedStats previously = poll[loop];
				devices.GetDSS(poll[loop], poll[loop].linearIndex);
				previously.linearIndex = poll[loop].linearIndex;
				if(previously == poll[loop] && previously != zero) continue;
				changes = true;
				linearIndex[linearIndex.size()] = previously.linearIndex;
				bad[bad.size()] = clamp(poll[loop].bad);
				good[good.size()] = clamp(poll[loop].good);
				stale[stale.size()] = clamp(poll[loop].stale);
			}
			return changes;
		}
	};
};


}
}
