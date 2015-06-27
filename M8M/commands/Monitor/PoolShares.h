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


class PoolShares : public AbstractStreamingCommand {
public:
	struct ShareStats {
		aulong sent, accepted, rejected;
        adouble daps; //!< difficulty accepted per second, finer grained WRT to device

        std::chrono::system_clock::time_point lastActivated;
        auint numActivationAttempts = 0;
        std::chrono::system_clock::duration cumulatedTime;
        std::chrono::system_clock::time_point lastSubmitReply, lastActivity;

		ShareStats() : sent(0), accepted(0), rejected(0), daps(.0) { }
        bool operator!=(const ShareStats &other) const {
            return sent != other.sent || accepted != other.accepted || rejected != other.rejected || daps != other.daps ||
                lastSubmitReply != other.lastSubmitReply;
        }
	};
	class ValueSourceInterface {
	public:
		virtual ~ValueSourceInterface() { }
		virtual bool GetPoolShareStats(ShareStats &out, asizei poolIndex) = 0;
	};

	PoolShares(ValueSourceInterface &src) : workers(src), AbstractStreamingCommand("poolShares") { }


private:
	ValueSourceInterface &workers;

	AbstractInternalPush* NewPusher() { return new Pusher(workers); }
	
	class Pusher : public AbstractInternalPush {
		ValueSourceInterface &workers;
		std::vector<ShareStats> sent;

	public:
		Pusher(ValueSourceInterface &getters) : workers(getters) { }
		bool MyCommand(const std::string &signature) const { return strcmp(signature.c_str(), "poolShares") == 0; }
		std::string GetPushName() const { return std::string("poolShares"); }
		void SetState(const rapidjson::Value &input) {
			asizei count = 0; 
			ShareStats out;
			while(workers.GetPoolShareStats(out, count)) count++;
			sent.resize(count);
		}
		bool RefreshAndReply(rapidjson::Document &build, bool changes) {
			using namespace rapidjson;
			build.SetArray();
            build.Reserve(SizeType(sent.size()), build.GetAllocator());
            for(asizei check = 0; check < sent.size(); check++) {
                ShareStats out;
                workers.GetPoolShareStats(out, check);
                Value add(kObjectType);
                if(out != sent[check] || changes) {
                    changes = true;
                    break;
                }
            }
            for(asizei check = 0; check < sent.size(); check++) {
                ShareStats out;
                workers.GetPoolShareStats(out, check);
                Value add(kObjectType);
                if(out != sent[check] || changes) {
                    changes = true;
                    auto &alloc(build.GetAllocator());
                    if(out.sent != sent[check].sent || changes) add.AddMember("sent", out.sent, build.GetAllocator());
                    if(out.accepted != sent[check].accepted || changes) add.AddMember("accepted", out.accepted, build.GetAllocator());
                    if(out.rejected != sent[check].rejected || changes) add.AddMember("rejected", out.rejected, build.GetAllocator());
                    using namespace std::chrono;
                    if(out.lastActivated != sent[check].lastActivated || changes) {
                        auto when(duration_cast<seconds>(out.lastActivated.time_since_epoch()));
                        add.AddMember("active", when.count(), build.GetAllocator());
                    }
                    if(out.daps != sent[check].daps || changes) add.AddMember("daps", out.daps, build.GetAllocator());
                    auto last = duration_cast<seconds>(        out.lastSubmitReply.time_since_epoch());
                    auto prev = duration_cast<seconds>(sent[check].lastSubmitReply.time_since_epoch());
                    if(last != prev || changes) add.AddMember(StringRef("lastSubmitReply"), last.count(), build.GetAllocator());
                    last = duration_cast<seconds>(        out.lastActivity.time_since_epoch());
                    prev = duration_cast<seconds>(sent[check].lastActivity.time_since_epoch());
                    if(last != prev || changes) add.AddMember(StringRef("lastActivity"), last.count(), build.GetAllocator());
                    if(out.numActivationAttempts != sent[check].numActivationAttempts || changes) add.AddMember("numActivations", out.numActivationAttempts, alloc);
                    if(out.cumulatedTime != sent[check].cumulatedTime || changes) {
                        auto howMuch(duration_cast<seconds>(out.cumulatedTime));
                        add.AddMember("cumulatedTime", howMuch.count(), alloc);
                    }
                    sent[check] = out;
                }
                build.PushBack(add, build.GetAllocator());
            }
			return changes;
		}
	};
};


}
}
