/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractStreamingCommand.h"
#include <memory>
#include "../../MiningProfiling.h"

namespace commands {
namespace monitor {

class Profiler : public AbstractStreamingCommand {
public:
	Profiler(MiningProfilerInterface &src) : profiler(src), AbstractStreamingCommand("profiler?") { }

private:
	MiningProfilerInterface &profiler;

	AbstractInternalPush* NewPusher() { return new Pusher(profiler); }	
	class Pusher : public AbstractInternalPush {
		MiningProfilerInterface &profiler;
	public:
		Pusher(MiningProfilerInterface &src) : profiler(src) { }
		bool MyCommand(const std::string &signature) const { return strcmp(signature.c_str(), "profiler!") == 0; }
		std::string GetPushName() const { return std::string("profiler!"); }
		ReplyAction SetState(std::string &error, const Json::Value &input) {
			const Json::Value &what(input["what"]);
			if(what.isArray() == false) { error = "\"what\" must be an array";    return ra_bad; }
			bool iterationTime = false;
			for(asizei test = 0; test < what.size(); test++) {
				if(what[test].isConvertibleTo(Json::stringValue) && what[test].asString() == "iterationTime") {
					iterationTime = true;
					break;
				}
			}
			if(!iterationTime) return ra_delete;
			return ra_consumed;
		}
		bool RefreshAndReply(Json::Value &build, bool changes) {
			MiningProfilerInterface::MPSamples samples;
			if(!profiler.Pop(samples)) {
				if(changes) {
					build = Json::Value(Json::objectValue);
					build["ignore"] = true;
					return true;
				}
				return false;
			}

			auto clamp = [](aulong value) -> auint { 
				aulong biggest(aulong(auint(~0)));
				return auint(value < biggest? value : biggest);
			};
			build = Json::Value(Json::objectValue);
			build["sinceEpoch"] = clamp(samples.sinceEpoch.count());
			build["device"] = Json::Value(Json::arrayValue);
			build["relative"] = Json::Value(Json::arrayValue);
			build["iterationTime"] = Json::Value(Json::arrayValue);
			const asizei entries = samples.interleaved.size() / 3;
			Json::Value &device(build["device"]);
			Json::Value &relative(build["relative"]);
			Json::Value &iterationTime(build["iterationTime"]);
			device.resize(entries);
			relative.resize(entries);
			iterationTime.resize(entries);
			for(asizei loop = 0; loop < entries; loop++) {
				auint li, when, iter;
				samples.Get(li, when, iter, loop);
				device[loop] = li;
				relative[loop] = when;
				iterationTime[loop] = iter;
			}
			return true;
		}
	};
};


}
}
