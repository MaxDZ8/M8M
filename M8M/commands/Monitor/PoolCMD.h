/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <json/json.h>
#include "../../MinerInterface.h"
#include "../AbstractCommand.h"
#include "../../../Common/AbstractWorkSource.h"

namespace commands {
namespace monitor {


class PoolCMD : public AbstractCommand {
	MinerInterface &miner;
public:
	typedef std::function<std::string(const AbstractWorkSource&)> PoolURLFunc;
	const PoolURLFunc getPoolURL;

	PoolCMD(MinerInterface &worker, const PoolURLFunc &getName) : miner(worker), getPoolURL(getName), AbstractCommand("pool") { }
	PushInterface* Parse(Json::Value &build, const Json::Value &input) {
		// Specification mandates there should be 1 parameter of value "primary".
		// This is not really required (works perfectly with no params at all) but required for being future proof.
		std::string mode("primary");
		if(input["params"].isNull()) { }
		else if(input["params"].isString()) { mode = input["params"].asString(); }
		else {
			build = "!!ERROR: \"parameters\" specified, but not a valid format.";
			return nullptr;
		}
		if(mode != "primary") {
			build = "!!ERROR: \"parameters\" unrecognized value \"" + mode + "\"";
			return nullptr;
		}

		const AbstractWorkSource *pool = miner.GetCurrentPool();
		if(!pool) return nullptr;
		build = Json::Value(Json::objectValue);
		build["name"] = pool->GetName();
		build["url"] = getPoolURL(*pool);
		
		std::vector< std::pair<const char*, StratumState::AuthStatus> > workers;
		pool->GetUserNames(workers);
		if(workers.size()) {
			build["users"] = Json::Value(Json::arrayValue);
			Json::Value &arr(build["users"]);
			for(asizei loop = 0; loop < workers.size(); loop++) {
				arr[loop] = Json::Value(Json::objectValue);
				Json::Value &add(arr[loop]);
				add["login"] = workers[loop].first;
				switch(workers[loop].second) {
				case StratumState::as_accepted: add["authorized"] = true; break;
				case StratumState::as_failed: add["authorized"] = false; break;
				case StratumState::as_inferred: add["authorized"] = "inferred"; break;
				case StratumState::as_pending: add["authorized"] = "pending"; break;
				case StratumState::as_notRequired: add["authorized"] = "open"; break;
				default: throw std::exception("Code out of sync? This is impossible!");
				}
			}
		}
		return nullptr;
	}
};


}
}
