/*
 * Copyright (C) 2015 Massimo Del Zotto
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "PoolManager.h"


void PoolManager::AddPools(const std::vector< std::unique_ptr<PoolInfo> > &all) {
    for(const auto &entry : all) {
        pools.push_back(std::move(Pool()));
        pools.back().config = *entry;
    }
}


void PoolManager::InitPools(const AbstractWorkSource::AlgoInfo &info) {
    for(auto &entry : pools) {
        if(_stricmp(entry.config.algo.c_str(), info.name.c_str())) continue;
        auto diff(std::make_pair(entry.config.diffMode, entry.config.diffMul));
        entry.source = std::make_unique<WorkSource>(entry.config.name, info, diff, entry.config.merkleMode);
        entry.source->AddCredentials(entry.config.user, entry.config.pass);
        entry.source->errorCallback = stratErrFunc;
    }
}


asizei PoolManager::Activate(const std::string &algo) {
    asizei activated = 0;
    for(auto &entry : pools) {
        if(!entry.source) throw std::string("Forgot to call InitPools(") + entry.config.algo + ')';
        if(_stricmp(entry.source->algo.name.c_str(), algo.c_str())) {
            if(entry.route) {
                entry.source->Shutdown();
                network.CloseConnection(*entry.route);
                entry.route = nullptr;
            }
            auto zero = std::chrono::system_clock::time_point();
            if(entry.activated != zero) {
                entry.totalTime += std::chrono::system_clock::now() - entry.activated;
                entry.activated = zero;
            }
        }
        else if(entry.route) {
            // A spurious call. Let's just ignore it, when connection will complete, it will be activated.
            // Or perhaps not, if connection fails but that's nothing I can fix there!
        }
        else {
            const char *port = entry.config.explicitPort.length()? entry.config.explicitPort.c_str() : entry.config.service.c_str();
            auto conn(network.BeginConnection(entry.config.host.c_str(), port));
            if(conn.first == nullptr) {
                if(connStateFunc) connStateFunc(*entry.source, MapError(conn.second));
                // A failing connection is a very bad thing. It probably just makes sense to bail out but in case it's transient,
                // let's retry with a multiple of retry delay. It's really odd so give it quite some time to clear out.
                entry.nextReconnect = std::chrono::system_clock::now() + reconnDelay * 4;
            }
            else {
                entry.route = conn.first;
                activated++;
                if(connStateFunc) connStateFunc(*entry.source, ce_connecting);
            }
            // note pool is not activated yet; they are activated in Refresh() when their connection is ready.
        }
    }
    return activated;
}


void PoolManager::FillSleepLists(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
    for(const auto &server : pools) {
        if(server.route == nullptr) continue;
        if(server.source->Ready() == false || // still connecting
            server.source->NeedsToSend()) toWrite.push_back(server.route); 
		else toRead.push_back(server.route); // that is, I give priority to sending over reading, only one per tick
	}
}


void PoolManager::Refresh(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
    // First of all, shut down pools whose connection has gone down.
    auto goodbye = [this](Network::SocketInterface *test) {
        if(!test) return; // this happens as Network::SleepOn clears dormient sockets.
        if(test->Works()) return;
        auto entry(std::find_if(pools.begin(), pools.end(), [test](Pool &entry) { return entry.route == test; }));
        if(entry == pools.end()) return; // this could be for example a mini-server web socket
        if(connStateFunc) connStateFunc(*entry->source, ce_failed);
        entry->source->Disconnected();
        network.CloseConnection(*entry->route);
        entry->route = nullptr;
        
        auto zero = std::chrono::system_clock::time_point();
        if(entry->activated != zero) {
            auto now(std::chrono::system_clock::now());
            entry->totalTime += now - entry->activated;
            entry->activated = zero;
            entry->nextReconnect = now + reconnDelay;
        }
    };
    for(auto entry : toRead) goodbye(entry);
    for(auto entry : toWrite) goodbye(entry);
    // Then do proper IO.
    auto activated = [](const std::vector<Network::SocketInterface*> &sockets, Network::SocketInterface *check) {
        return std::find(sockets.cbegin(), sockets.cend(), check) != sockets.cend();
    };
    for(auto &entry : pools) {
        if(entry.source->Ready() == false) continue;
        auto r(activated(toRead, entry.route));
        auto w(activated(toWrite, entry.route));
        if(!w && !r) continue;

        auto &pool(*entry.source);
        auto happens(pool.Refresh(r, w));
        if(happens.connFailed) goodbye(entry.route);
        else {
            if(happens.bytesReceived && onPoolCommand) onPoolCommand(pool);
            if(happens.diffChanged && diffChangeFunc) diffChangeFunc(pool, pool.GetCurrentDiff());
            if(happens.newWork && dispatchFunc) dispatchFunc(pool, std::unique_ptr<stratum::AbstractWorkFactory>(pool.GenWork()));
        }
    }
    // Then initialize pools which have just connected.
    for(auto &entry : pools) {
        if(entry.source->Ready() == true) continue; // already fully enabled, not connecting
        if(entry.route == nullptr) continue; // disabled, different algo, not even trying
        if(!activated(toWrite, entry.route)) continue;
        entry.activated = std::chrono::system_clock::now();
        entry.numActivations++;
        entry.source->Use(entry.route); // what if connection failed? Nothing. We try anyway and then bail out.
    }
    AttemptReconnections();
}


PoolManager::~PoolManager() {
    for(auto &server : pools) {
        server.source->Shutdown();
        if(server.route) network.CloseConnection(*server.route);
    }
}


void PoolManager::SetReconnectDelay(std::chrono::seconds delay) {
    reconnDelay = delay;
    std::chrono::seconds adjust = delay - reconnDelay;
    for(auto &pool : pools) {
        if(pool.nextReconnect != std::chrono::system_clock::time_point()) pool.nextReconnect += adjust;
    }
}


std::vector< std::pair<const char*, StratumState::AuthStatus> > PoolManager::GetWorkerAuthState(asizei i) const {
    std::vector< std::pair<const char*, StratumState::AuthStatus> > ret;
    if(pools[i].source) pools[i].source->GetUserNames(ret);
    else { // this happens if this is called before Connect is called or if a certain pool cannot be initialized as not eligible
        //! \todo perhaps I should just build all the pools?
        ret.push_back(std::make_pair(pools[i].config.user.c_str(), StratumState::as_off));
    }
    return ret;
}


asizei PoolManager::AttemptReconnections() {
    asizei restarted = 0;
    auto now(std::chrono::system_clock::now());
    auto zero = std::chrono::system_clock::time_point(); // for some reason writing this with init syntax makes Intellisense think it's a function
    for(auto &entry : pools) {
        if(entry.nextReconnect == zero) continue;
        if(entry.nextReconnect > now) continue;
        
        const char *port = entry.config.explicitPort.length()? entry.config.explicitPort.c_str() : entry.config.service.c_str();
        auto conn(network.BeginConnection(entry.config.host.c_str(), port));
        if(conn.first == nullptr) {
            if(connStateFunc) connStateFunc(*entry.source, MapError(conn.second));
            // A failing connection is a very bad thing. It probably just makes sense to bail out but in case it's transient,
            // let's retry with a multiple of retry delay. It's really odd so give it quite some time to clear out.
            entry.nextReconnect = std::chrono::system_clock::now() + reconnDelay * 4;
        }
        else {
            entry.route = conn.first;
            restarted++;
            entry.nextReconnect = zero;
            if(connStateFunc) connStateFunc(*entry.source, ce_connecting);
        }
    }
    return restarted;
}


auto PoolManager::MapError(NetworkInterface::ConnectionError ce) -> ConnectionEvent {
    switch(ce) {
    case NetworkInterface::ce_failedResolve: return ce_failedResolve;
    case NetworkInterface::ce_badSocket: return ce_badSocket;
    case NetworkInterface::ce_failedConnect: return ce_failedConnect;
    case NetworkInterface::ce_noRoutes: return ce_noRoutes;
    }
    throw std::exception("Something went very wrong with connection event mappings, code out of sync");
}
