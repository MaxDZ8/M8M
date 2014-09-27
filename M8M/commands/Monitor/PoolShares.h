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
	class ShareProviderInterface {
	public:
		virtual ~ShareProviderInterface() { }
		virtual asizei GetNumPools() const = 0;
		virtual std::string GetPoolName(asizei pool) const = 0;
		virtual asizei GetNumWorkers(asizei pool) const = 0;
		virtual StratumState::WorkerNonceStats GetWorkerStats(asizei poolIndex, asizei workerIndex) = 0;
	};

	PoolShares(ShareProviderInterface &src) : workers(src), AbstractStreamingCommand("poolShares") { }


private:
	ShareProviderInterface &workers;

	AbstractInternalPush* NewPusher() { return new Pusher(workers); }
	
	class Pusher : public AbstractInternalPush {
		ShareProviderInterface &workers;
		std::vector<StratumState::WorkerNonceStats> sent;

	public:
		Pusher(ShareProviderInterface &getters) : workers(getters) { }
		bool MyCommand(const std::string &signature) const { return strcmp(signature.c_str(), "poolShares") == 0; }
		std::string GetPushName() const { return std::string("poolShares"); }
		void SetState(const rapidjson::Value &input) { /* it's either enabled or not */ }
		bool RefreshAndReply(rapidjson::Document &build, bool changes) {
			changes |= (sent.size() == 0 && workers.GetNumPools() != 0);
			if(sent.size() == 0 && workers.GetNumPools() != 0) {
				asizei sum = 0;
				for(asizei loop = 0; loop < workers.GetNumPools(); loop++) sum += workers.GetNumWorkers(loop);
				sent.resize(sum);
			}
			asizei slot = 0;
			for(asizei p = 0; p < workers.GetNumPools(); p++) {
				for(asizei w = 0; w < workers.GetNumWorkers(p); w++) {
					const StratumState::WorkerNonceStats get = workers.GetWorkerStats(p, w);
					changes |= (get != sent[slot]);
					sent[slot] = get;
					slot++;
				}
			}
			if(changes) {
				slot = 0;
				using namespace rapidjson;
				build.SetObject();
				for(asizei p = 0; p < workers.GetNumPools(); p++) {
					std::string pname(workers.GetPoolName(p));
					build.AddMember(Value(pname.c_str(), pname.length(), build.GetAllocator()), Value(kObjectType), build.GetAllocator());
					Value &pool(build[workers.GetPoolName(p).c_str()]);
					pool.AddMember("sent", Value(kArrayType), build.GetAllocator());
					pool.AddMember("accepted", Value(kArrayType), build.GetAllocator());
					pool.AddMember("rejected", Value(kArrayType), build.GetAllocator());
					Value &sent(pool["sent"]);
					Value &accepted(pool["accepted"]);
					Value &rejected(pool["rejected"]);
					for(asizei w = 0; w < workers.GetNumWorkers(p); w++) {
						sent.PushBack(this->sent[slot].sent, build.GetAllocator());
						accepted.PushBack(this->sent[slot].accepted, build.GetAllocator());
						rejected.PushBack(this->sent[slot].rejected, build.GetAllocator());
						slot++;
					}
				}
			}
			return changes;
		}
	};
};


}
}
