/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCommand.h"
#include "../../Connections.h"

namespace commands {
namespace monitor {


class PoolCMD : public AbstractCommand {
public:
    const Connections &conn;

	PoolCMD(const Connections &remote) : conn(remote), AbstractCommand("pools") { }
	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		using namespace rapidjson;
		build.SetArray();
        if(conn.GetNumServers() == 0) return nullptr;
        for(asizei serv = 0; serv < conn.GetNumServers(); serv++) {
            const auto &pool(conn.GetServer(serv));
            const auto &connInfo(conn.GetConnection(pool));
            Value add(kObjectType);
            const auto url(connInfo.PeerHost() + ':' + connInfo.PeerPort());
            add.AddMember("name", StringRef(pool.name.c_str()), build.GetAllocator());
		    add.AddMember("url", Value(url.c_str(), SizeType(url.length()), build.GetAllocator()), build.GetAllocator());
            add.AddMember("algo", StringRef(pool.algo.name.c_str()), build.GetAllocator());
            add.AddMember("protocol", "stratum", build.GetAllocator());
            add.AddMember("diffMul", jsonify(pool.GetDiffMultipliers(), build.GetAllocator()), build.GetAllocator());

            Value users(kArrayType);
            std::vector< std::pair<const char*, StratumState::AuthStatus> > workers;
		    pool.GetUserNames(workers);
            for(const auto &worker : workers) users.PushBack(jsonify(worker, build.GetAllocator()), build.GetAllocator());
            add.AddMember("users", users, build.GetAllocator());
            build.PushBack(add, build.GetAllocator());
        }
		return nullptr;
	}

private:
    static rapidjson::Value jsonify(const PoolInfo::DiffMultipliers &mul, rapidjson::MemoryPoolAllocator<> &alloc) {
        using namespace rapidjson;
        Value ret(kObjectType);
        ret.AddMember("stratum", mul.stratum, alloc);
        ret.AddMember("one", mul.one, alloc);
        ret.AddMember("share", mul.share, alloc);
        return ret;
    }
    static rapidjson::Value jsonify(const std::pair<const char*, StratumState::AuthStatus> &auth, rapidjson::MemoryPoolAllocator<> &alloc) {
        using namespace rapidjson;
		Value entry(kObjectType);
		entry.AddMember("login", Value(StringRef(auth.first)), alloc);
        Value status(kStringType);
		switch(auth.second) {
		case StratumState::as_accepted: status.SetBool(true); break;
		case StratumState::as_failed: status.SetBool(false); break;
		case StratumState::as_inferred: status = "inferred"; break;
		case StratumState::as_pending: status = "pending"; break;
		case StratumState::as_notRequired: status = "open"; break;
		default: throw std::exception("Code out of sync? This is impossible!");
		}
        entry.AddMember("authorized", status, alloc);
        return entry;
    }
};


}
}
