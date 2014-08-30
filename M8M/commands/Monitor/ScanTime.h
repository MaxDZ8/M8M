/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractStreamingCommand.h"
#include <chrono>

namespace commands {
namespace monitor {

/*! The first truly interesting command. Besides providing a reply, can optionally initiate data streaming on change. */
class ScanTime : public AbstractStreamingCommand {
public:
	class DeviceTimeProviderInterface {
	public:
		virtual ~DeviceTimeProviderInterface() { }
		// Return time appropriately based for a device matching its linear index.
		// Return false for device not found - if a client requests unmatched device it is his business, not ours. Values unused.
		virtual bool GetSTMinMax(std::chrono::microseconds &min, std::chrono::microseconds &max, asizei devIndex) const = 0;
		virtual bool GetSTSlidingAvg(std::chrono::microseconds &window, asizei devIndex) const = 0;
		virtual bool GetSTLast(std::chrono::microseconds &last, std::chrono::microseconds &avg, asizei devIndex) const = 0;

		//! Time window for computing averages. Must be constant across all devices.
		virtual void GetSTWindow(std::chrono::minutes &window) const = 0;
	};

	ScanTime(DeviceTimeProviderInterface &src) : devices(src), AbstractStreamingCommand("scanTime?") { }


private:
	DeviceTimeProviderInterface &devices;
	AbstractInternalPush* NewPusher() { return new Pusher(devices); }

	struct DevStats {
		std::chrono::milliseconds min, max;
		std::chrono::milliseconds slidingAvg, slrAvg;
		std::chrono::milliseconds last;
		asizei linearIndex;
	};
	
	class Pusher : public AbstractInternalPush {
		DeviceTimeProviderInterface &devices;
		std::vector<DevStats> poll;
		struct Enable {
			bool min, max, slidingAvg, slrAvg, last;
			Enable() { min = max = slidingAvg = slrAvg = last = false; }
		} enabled;
	public:
		Pusher(DeviceTimeProviderInterface &getters) : devices(getters) { }
		bool MyCommand(const std::string &signature) const { return strcmp(signature.c_str(), "scanTime!") == 0; }
		std::string GetPushName() const { return std::string("scanTime!"); }

		ReplyAction SetState(std::string &error, const Json::Value &input) {
			const Json::Value &index(input["devices"]), &what(input["requesting"]);
			if(!index.isArray()) { error = "\"devices\" must be an array of devices to track";    return ra_bad; }
			if(!what.isObject()) { error = "\"requesting\" must be an object containing one bool for each property to monitor";    return ra_bad; }
			asizei numDevices = index.size();
			if(!numDevices) return ra_delete;
			for(asizei test = 0; test < numDevices; test++) {
				if(index[test].isConvertibleTo(Json::uintValue) == false) {
					error = "\"devices[" + std::to_string(test) + "]\" is not a valid index";
					return ra_bad;
				}
			}
			poll.resize(numDevices);
			for(asizei loop = 0; loop < numDevices; loop++) poll[loop].linearIndex = index[loop].asUInt();
			#define ENABLE(WHAT) if(what[#WHAT].isNull() == false && what[#WHAT].isConvertibleTo(Json::booleanValue)) enabled.WHAT = what[#WHAT].asBool();
			ENABLE(min);
			ENABLE(max);
			ENABLE(slidingAvg);
			ENABLE(slrAvg);
			ENABLE(last);
			#undef ENABLE
			return ra_consumed;
		}
		bool RefreshAndReply(Json::Value &build, bool changes) {
			using namespace std::chrono;
			minutes shortw;
			devices.GetSTWindow(shortw);
			auto clamp = [](aulong value) -> auint { 
				aulong biggest(aulong(auint(~0)));
				return auint(value < biggest? value : biggest);
			};
			Changer change(changes);
			build["twindow"] = clamp(shortw.count());
			build["measurements"] = Json::Value(Json::arrayValue);
			Json::Value &arr(build["measurements"]);
			#define MANGLE(WHAT) if(enabled.WHAT && dev.WHAT != WHAT) { \
				aulong ref = dev.WHAT.count(); \
				dev.WHAT = duration_cast<milliseconds>(WHAT); \
				change.Set(yours, #WHAT, clamp(ref), clamp(dev.WHAT.count())); \
			}
                //!< \todo another cast due to jsoncpp not having long long

			for(asizei loop = 0; loop < poll.size(); loop++) {
				DevStats &dev(poll[loop]);
				microseconds min, max, slidingAvg, slrAvg, last;
				minutes swin, lwin;
				devices.GetSTSlidingAvg(slidingAvg, dev.linearIndex);
				devices.GetSTMinMax(min, max, dev.linearIndex);
				devices.GetSTLast(last, slrAvg, dev.linearIndex);
				Json::Value yours(Json::objectValue);
				MANGLE(min);
				MANGLE(max);
				MANGLE(slidingAvg);
				MANGLE(slrAvg);
				MANGLE(last);
				arr[arr.size()] = yours; // I always add an object, even if empty so client has it easy!
			}
			#undef MANGLE
			return changes;
		}
	};
};


}
}
