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
		void SetState(const Json::Value &input) { /* it's either enabled or not */ }
		bool RefreshAndReply(Json::Value &build, bool changes) {
			changes |= sent.size() == 0;
			if(sent.size() == 0) {
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
				build = Json::Value(Json::objectValue);
				for(asizei p = 0; p < workers.GetNumPools(); p++) {
					Json::Value &pool(build[workers.GetPoolName(p)]);
					Json::Value &sent(pool["sent"]);
					Json::Value &accepted(pool["accepted"]);
					Json::Value &rejected(pool["rejected"]);
					for(asizei w = 0; w < workers.GetNumWorkers(p); w++) {
						sent[sent.size()] = this->sent[slot].sent;
						accepted[accepted.size()] = this->sent[slot].accepted;
						rejected[rejected.size()] = this->sent[slot].rejected;
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
