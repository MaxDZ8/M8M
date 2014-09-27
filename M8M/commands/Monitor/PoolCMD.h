/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
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
	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		// Specification mandates there should be 1 parameter of value "primary".
		// This is not really required (works perfectly with no params at all) but required for being future proof.
		std::string mode("primary");
		using namespace rapidjson;
		Value::ConstMemberIterator params = input.FindMember("params");
		if(params == input.MemberEnd() || params->value.IsNull()) { }
		else if(params->value.IsString()) { mode.assign(params->value.GetString(), params->value.GetStringLength()); }
		else {
			build.SetString("!!ERROR: \"parameters\" specified, but not a valid format.");
			return nullptr;
		}
		if(mode != "primary") {
			const string msg("!!ERROR: \"parameters\" unrecognized value \"" + mode + "\"");
			build.SetString(msg.c_str(), msg.length(), build.GetAllocator());
			return nullptr;
		}

		const AbstractWorkSource *pool = miner.GetCurrentPool();
		build.SetObject();
		if(!pool) return nullptr;
		build.AddMember("name", Value(kStringType), build.GetAllocator());
		build.AddMember("url", Value(kStringType), build.GetAllocator());
		build["name"].SetString(pool->GetName(), strlen(pool->GetName()));
		const std::string url(getPoolURL(*pool));
		build["url"].SetString(url.c_str(), url.length(), build.GetAllocator());
		
		std::vector< std::pair<const char*, StratumState::AuthStatus> > workers;
		pool->GetUserNames(workers);
		if(workers.size()) {
			build.AddMember("users",  Value(kArrayType), build.GetAllocator());
			Value &arr(build["users"]);
			for(asizei loop = 0; loop < workers.size(); loop++) {
				Value add(kObjectType);
				add.AddMember("login", Value(kStringType), build.GetAllocator());
				add.AddMember("authorized", Value(kStringType), build.GetAllocator());
				add["login"].SetString(workers[loop].first, strlen(workers[loop].first)); //!< \todo why is SetString(char*) instead of SetString(const char*)?
				Value &auth(add["authorized"]);
				switch(workers[loop].second) {
				case StratumState::as_accepted: auth.SetBool(true); break;
				case StratumState::as_failed: auth.SetBool(false); break;
				case StratumState::as_inferred: auth = "inferred"; break;
				case StratumState::as_pending: auth = "pending"; break;
				case StratumState::as_notRequired: auth = "open"; break;
				default: throw std::exception("Code out of sync? This is impossible!");
				}
				arr.PushBack(add, build.GetAllocator());
			}
		}
		return nullptr;
	}
};


}
}
