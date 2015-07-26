/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractStreamingCommand.h"
#include "../../../Common/StratumState.h"
#include <memory>

namespace commands {
namespace monitor {


class PoolStats : public AbstractStreamingCommand {
public:
	struct ShareStats {
		aulong sent, accepted, rejected;
        adouble daps; //!< difficulty accepted per second, finer grained WRT to device

        std::chrono::system_clock::time_point lastActivated, lastConnDown;
        auint numActivationAttempts = 0;
        std::chrono::system_clock::duration cumulatedTime;
        std::chrono::system_clock::time_point lastSubmitReply, lastActivity;

		ShareStats() : sent(0), accepted(0), rejected(0), daps(.0) { }
        bool operator!=(const ShareStats &other) const {
            return sent != other.sent || accepted != other.accepted || rejected != other.rejected || daps != other.daps ||
                lastActivated != other.lastActivated || lastConnDown != other.lastConnDown ||
                numActivationAttempts != other.numActivationAttempts || cumulatedTime != other.cumulatedTime ||
                lastSubmitReply != other.lastSubmitReply || lastActivity != other.lastActivity;
        }
	};
	class ValueSourceInterface {
	public:
		virtual ~ValueSourceInterface() { }
		virtual bool GetPoolShareStats(ShareStats &out, asizei poolIndex) = 0;
	};

	PoolStats(ValueSourceInterface &src) : workers(src), AbstractStreamingCommand("poolStats") { }


private:
	ValueSourceInterface &workers;

	AbstractInternalPush* NewPusher() { return new Pusher(workers); }

	class Pusher : public AbstractInternalPush {
		ValueSourceInterface &workers;
		std::vector<ShareStats> sent;

	public:
		Pusher(ValueSourceInterface &getters) : workers(getters) { }
		bool MyCommand(const std::string &signature) const { return strcmp(signature.c_str(), "poolStats") == 0; }
		std::string GetPushName() const { return std::string("poolStats"); }
		void SetState(const rapidjson::Value &input) {
			asizei count = 0;
			ShareStats out;
			while(workers.GetPoolShareStats(out, count)) count++;
			sent.resize(count);
		}
		bool RefreshAndReply(rapidjson::Document &build, bool changes) {
			using namespace rapidjson;
            auto &alloc(build.GetAllocator());
			build.SetArray();
            build.Reserve(SizeType(sent.size()), alloc);
            asizei differences = 0;
            for(asizei check = 0; check < sent.size(); check++) {
                ShareStats out;
                workers.GetPoolShareStats(out, check);
                Value add(kObjectType);
                if(out != sent[check] || changes) {
                    differences++;
                    Value add(kObjectType);
                    auto &alloc(alloc);
                    if(out.sent != sent[check].sent || changes) add.AddMember("sent", out.sent, alloc);
                    if(out.accepted != sent[check].accepted || changes) add.AddMember("accepted", out.accepted, alloc);
                    if(out.rejected != sent[check].rejected || changes) add.AddMember("rejected", out.rejected, alloc);
                    using namespace std::chrono;
                    if(out.lastActivated != sent[check].lastActivated || changes) {
                        auto when(duration_cast<seconds>(out.lastActivated.time_since_epoch()));
                        add.AddMember("activated", when.count(), alloc);
                    }
                    if(out.lastConnDown != sent[check].lastConnDown || changes) {
                        auto when(duration_cast<seconds>(out.lastConnDown.time_since_epoch()));
                        add.AddMember("lastConnDown", when.count(), alloc);
                    }
                    if(out.daps != sent[check].daps || changes) add.AddMember("daps", out.daps, alloc);
                    auto last = duration_cast<seconds>(        out.lastSubmitReply.time_since_epoch());
                    auto prev = duration_cast<seconds>(sent[check].lastSubmitReply.time_since_epoch());
                    if(last != prev || changes) add.AddMember(StringRef("lastSubmitReply"), last.count(), alloc);
                    last = duration_cast<seconds>(        out.lastActivity.time_since_epoch());
                    prev = duration_cast<seconds>(sent[check].lastActivity.time_since_epoch());
                    if(last != prev || changes) add.AddMember(StringRef("lastActivity"), last.count(), alloc);
                    if(out.numActivationAttempts != sent[check].numActivationAttempts || changes) add.AddMember("numActivations", out.numActivationAttempts, alloc);
                    if(out.cumulatedTime != sent[check].cumulatedTime || changes) {
                        auto howMuch(duration_cast<seconds>(out.cumulatedTime));
                        add.AddMember("cumulatedTime", howMuch.count(), alloc);
                    }
                    sent[check] = out;
                    build.PushBack(add, alloc);
                }
                else build.PushBack(Value(kNullType), alloc);
            }
			return differences != 0;
		}
	};
};


}
}
