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

	ScanTime(DeviceTimeProviderInterface &src) : devices(src), AbstractStreamingCommand("scanTime") { }


private:
	DeviceTimeProviderInterface &devices;
	AbstractInternalPush* NewPusher() { return new Pusher(devices); }

	struct DevStats {
		std::chrono::milliseconds min, max;
		std::chrono::milliseconds slidingAvg, slrAvg;
		std::chrono::milliseconds last;
	};
	
	class Pusher : public AbstractInternalPush {
		DeviceTimeProviderInterface &devices;
		std::vector<DevStats> poll;
	public:
		Pusher(DeviceTimeProviderInterface &getters) : devices(getters) { }
		bool MyCommand(const std::string &signature) const { return strcmp(signature.c_str(), "scanTime!") == 0; }
		std::string GetPushName() const { return std::string("scanTime!"); }

		void SetState(const rapidjson::Value &input) {
			asizei count = 0; 
			std::chrono::microseconds foo, bar;
			while(devices.GetSTLast(foo, bar, count)) count++;
			poll.resize(count);
		}
		bool RefreshAndReply(rapidjson::Document &build, bool changes) {
			using namespace std::chrono;
			using namespace rapidjson;
			minutes shortw;
			devices.GetSTWindow(shortw);
			build.SetObject();
			build.AddMember("twindow", Value(shortw.count()), build.GetAllocator());
			build.AddMember("measurements", rapidjson::Value(rapidjson::kArrayType), build.GetAllocator());
			rapidjson::Value &arr(build["measurements"]);
			for(asizei loop = 0; loop < poll.size(); loop++) {
				DevStats &dev(poll[loop]);
				microseconds min, max, slidingAvg, slrAvg, last;
				minutes swin, lwin;
				devices.GetSTSlidingAvg(slidingAvg, loop);
				devices.GetSTMinMax(min, max, loop);
				devices.GetSTLast(last, slrAvg, loop);
				bool update = false;
				update |= duration_cast<milliseconds>(min) != dev.min || duration_cast<milliseconds>(max) != dev.max;
				update |= duration_cast<milliseconds>(slidingAvg) != dev.slidingAvg;
                update |= duration_cast<milliseconds>(last) != dev.last || duration_cast<milliseconds>(slrAvg) != dev.slrAvg;
				if(update) {
					dev.min = duration_cast<milliseconds>(min);
					dev.max = duration_cast<milliseconds>(max);
					dev.slidingAvg = duration_cast<milliseconds>(slidingAvg);
					dev.last = duration_cast<milliseconds>(last);
					dev.slrAvg = duration_cast<milliseconds>(slrAvg);
				}
				changes |= update;
			}
            if(changes) {
                for(asizei loop = 0; loop < poll.size(); loop++) {
					rapidjson::Value yours(rapidjson::kObjectType);
				    yours.AddMember("min", Value(poll[loop].min.count()), build.GetAllocator());
				    yours.AddMember("max", Value(poll[loop].max.count()), build.GetAllocator());
				    yours.AddMember("slidingAvg", Value(poll[loop].slidingAvg.count()), build.GetAllocator());
				    yours.AddMember("slrAvg", Value(poll[loop].slrAvg.count()), build.GetAllocator());
				    yours.AddMember("last", Value(poll[loop].last.count()), build.GetAllocator());
					arr.PushBack(yours, build.GetAllocator()); // I don't use PushBack as it needs to explicitly provide an allocator
			    }
			}
			return changes;
		}
	};
};


}
}
