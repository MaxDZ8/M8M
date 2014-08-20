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


class CurrentPoolCMD : public AbstractCommand {
	MinerInterface &miner;
public:
	typedef std::function<std::string(const AbstractWorkSource&)> PoolURLFunc;
	const PoolURLFunc getPoolURL;

	CurrentPoolCMD(MinerInterface &worker, const PoolURLFunc &getName) : miner(worker), getPoolURL(getName), AbstractCommand("currentPool?") { }
	PushInterface* Parse(Json::Value &build, const Json::Value &input) {
		// no params
		const AbstractWorkSource *pool = miner.GetCurrentPool();
		if(!pool) return nullptr;
		build = Json::Value(Json::objectValue);
		build["name"] = pool->GetName();
		build["url"] = getPoolURL(*pool);
		
		std::vector< std::pair<const char*, bool> > workers;
		pool->GetUserNames(workers);
		if(workers.size()) {
			build["users"] = Json::Value(Json::arrayValue);
			Json::Value &arr(build["users"]);
			for(asizei loop = 0; loop < workers.size(); loop++) {
				arr[loop] = Json::Value(Json::objectValue);
				Json::Value &add(arr[loop]);
				add["login"] = workers[loop].first;
				add["authorized"] = workers[loop].second;
			}
		}
		return nullptr;
	}
};


}
}
