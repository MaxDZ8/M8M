/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractStreamingCommand.h"
#include <chrono>
#include "../../MiningPerformanceWatcher.h"

namespace commands {
namespace monitor {

/*! The first truly interesting command. Besides providing a reply, can optionally initiate data streaming on change. */
class ScanTime : public AbstractStreamingCommand {
public:
	ScanTime(MiningPerformanceWatcherInterface &src) : devices(src), AbstractStreamingCommand("scanTime") { }


private:
	MiningPerformanceWatcherInterface &devices;
	AbstractInternalPush* NewPusher() { return new Pusher(devices); }
	
	class Pusher : public AbstractInternalPush {
		MiningPerformanceWatcherInterface &devices;
		std::vector<MiningPerformanceWatcherInterface::DevStats> poll;

        static bool MaybeAddValue_ms(rapidjson::Value &container, const char *name, std::chrono::microseconds current, std::chrono::microseconds &old,
                                  bool force, rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator> &allocator) {
            bool changed = false;
            if(force || current != old) {
                auto ms(std::chrono::duration_cast<std::chrono::milliseconds>(current));
                changed = true;
                container.AddMember(rapidjson::StringRef(name), ms.count(), allocator);
                old = current;
            }
            return changed;
        }

	public:
        Pusher(MiningPerformanceWatcherInterface &getters) : devices(getters) { poll.resize(devices.GetNumDevices()); }
		bool MyCommand(const std::string &signature) const { return strcmp(signature.c_str(), "scanTime!") == 0; }
		std::string GetPushName() const { return std::string("scanTime!"); }

		void SetState(const rapidjson::Value &input) { }
		bool RefreshAndReply(rapidjson::Document &build, bool changes) {
			using namespace std::chrono;
			using namespace rapidjson;
			build.SetObject();
			build.AddMember("twindow", Value(devices.GetAverageWindow().count()), build.GetAllocator());
			build.AddMember("measurements", rapidjson::Value(rapidjson::kArrayType), build.GetAllocator());
			rapidjson::Value &arr(build["measurements"]);
            arr.Reserve(SizeType(poll.size()), build.GetAllocator());
            bool updated = false;
			for(asizei loop = 0; loop < poll.size(); loop++) {
                Value add(kObjectType);
                MiningPerformanceWatcherInterface::DevStats refreshed;
                if(devices.GetPerformance(refreshed, loop)) {
                    updated |= MaybeAddValue_ms(add, "min", refreshed.min, poll[loop].min, changes, build.GetAllocator());
                    updated |= MaybeAddValue_ms(add, "max", refreshed.max, poll[loop].max, changes, build.GetAllocator());
                    updated |= MaybeAddValue_ms(add, "avg", refreshed.avg, poll[loop].avg, changes, build.GetAllocator());
                    updated |= MaybeAddValue_ms(add, "last", refreshed.last, poll[loop].last, changes, build.GetAllocator());
                    arr.PushBack(add, build.GetAllocator());
                }
                else arr.PushBack(Value(kNullType), build.GetAllocator());
			}
			return changes || updated;
		}
	};
};


}
}
