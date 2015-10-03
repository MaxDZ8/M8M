/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "M8MPoolConnectingApp.h"

bool M8MPoolConnectingApp::AddPool(const PoolInfo &copy, const CanonicalInfo &algoInfo) {
    pools.push_back(Pool());
    pools.back().config = copy;
    pools.back().source = std::make_unique<WorkSource>(copy.name, algoInfo, std::make_pair(copy.diffMode, copy.diffMul), copy.merkleMode);
    auto &source(*pools.back().source);
    source.AddCredentials(copy.user, copy.pass);
    source.errorCallback = [this](const AbstractWorkSource &owner, asizei i, int errorCode, const std::string &message) {
        StratumError(owner, i, errorCode, message);
    };
    source.shareResponseCallback = [this](const AbstractWorkSource &me, asizei shareID, StratumShareResponse stat) {
        ShareResponse(me, shareID, stat);
    };
    source.workerAuthCallback = [this](const AbstractWorkSource &owner, const std::string &worker, StratumState::AuthStatus status) {
        WorkerAuthorization(owner, worker, status);
    };
    return true;
}


asizei M8MPoolConnectingApp::BeginPoolActivation(const char *algo) {
    asizei activated = 0;
    for(auto &entry : pools) {
        if(_stricmp(entry.config.algo.c_str(), algo)) { // different algos get disabled.
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
            activated++; // they still count however.
        }
        else {
            const char *port = entry.config.explicitPort.length()? entry.config.explicitPort.c_str() : entry.config.service.c_str();
            auto conn(network.BeginConnection(entry.config.host.c_str(), port));
            if(conn.first == nullptr) {
                ConnectionState(*entry.source, MapError(conn.second));
                // A failing connection is a very bad thing. It probably just makes sense to bail out but in case it's transient,
                // let's retry with a multiple of retry delay. It's really odd so give it quite some time to clear out.
                entry.nextReconnect = std::chrono::system_clock::now() + reconnectDelay * 4;
            }
            else {
                entry.route = conn.first;
                activated++;
                ConnectionState(*entry.source, ce_connecting);
            }
            // note pool is not activated yet; they are activated in Refresh() when their connection is ready.
        }
    }
    return activated;
}


void M8MPoolConnectingApp::FillSleepLists(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
    for(const auto &server : pools) {
        if(server.route == nullptr) continue;
        if(server.source->Ready() == false || // still connecting
            server.source->NeedsToSend()) toWrite.push_back(server.route);
        else toRead.push_back(server.route); // that is, I give priority to sending over reading, only one per tick
    }
}


void M8MPoolConnectingApp::Refresh(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
    M8MConfiguredApp::Tick();
    // First of all, shut down pools whose connection has gone down.
    auto goodbye = [this](Network::SocketInterface *test) {
        if(!test) return; // this happens as Network::SleepOn clears dormient sockets.
        if(test->Works()) return;
        auto entry(std::find_if(pools.begin(), pools.end(), [test](Pool &entry) { return entry.route == test; }));
        if(entry == pools.end()) return; // this could be for example a mini-server web socket
        ConnectionState(*entry->source, ce_failed);
        entry->source->Disconnected();
        network.CloseConnection(*entry->route);
        entry->route = nullptr;

        auto zero = std::chrono::system_clock::time_point();
        if(entry->activated != zero) {
            auto now(std::chrono::system_clock::now());
            entry->totalTime += now - entry->activated;
            entry->activated = zero;
            entry->nextReconnect = now + reconnectDelay;
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
            if(happens.bytesReceived) PoolCommand(pool);
            if(happens.diffChanged) DiffChange(pool, pool.GetCurrentDiff());
            if(happens.newWork) WorkChange(pool, std::unique_ptr<stratum::AbstractWorkFactory>(pool.GenWork()));
        }
    }
    // Then initialize pools which have just connected.
    for(auto &entry : pools) {
        if(entry.source->Ready()) continue; // already fully enabled, not connecting
        if(entry.route == nullptr) continue; // disabled, different algo, not even trying
        if(!activated(toWrite, entry.route)) continue;
        entry.activated = std::chrono::system_clock::now();
        entry.numActivations++;
        entry.source->Use(entry.route); // what if connection failed? Nothing. We try anyway and then bail out.
        ConnectionState(*entry.source, ce_ready);
    }
    AttemptReconnections();
}


void M8MPoolConnectingApp::SendResults(const NonceOriginIdentifier &from, const VerifiedNonces &sharesFound) {
    AbstractWorkSource *owner = nullptr;
    asizei poolIndex = 0;
    for(asizei search = 0; search < pools.size(); search++) {
        if(pools[search].source.get() == from.owner) {
            owner = pools[search].source.get();
            poolIndex = search;
            break;
        }
    }
    if(!owner) throw "Impossible, WU owner not found"; // really wrong stuff. Most likely a bug in code or possibly we just got hit by some cosmic ray
    if(sharesFound.wrong) BadHashes(*owner, sharesFound.device, sharesFound.wrong);
    auto ntime = owner->IsCurrentJob(from.job);
    if(ntime) {
        asizei sent = 0;
        for(auto &result : sharesFound.nonces) {
            ShareIdentifier shareSrc;
            shareSrc.owner = owner;
            shareSrc.shareIndex = owner->SendShare(from.job, ntime, sharesFound.nonce2, result.nonce);

            ShareFeedbackData fback;
            fback.block = result.block;
            fback.hashSlice = result.hashSlice;
            fback.shareDiff = result.diff;
            fback.targetDiff = sharesFound.targetDiff;
            fback.gpuIndex = sharesFound.device;

            sentShares.insert(std::make_pair(shareSrc, fback));
            sent++;
        }
        AddSent(*owner, sent);
    }
    else AddStale(sharesFound.device, sharesFound.nonces.size());
}


void M8MPoolConnectingApp::AttemptReconnections() {
    asizei restarted = 0;
    auto now(std::chrono::system_clock::now());
    auto zero = std::chrono::system_clock::time_point(); // for some reason writing this with init syntax makes Intellisense think it's a function
    for(auto &entry : pools) {
        if(entry.nextReconnect == zero) continue;
        if(entry.nextReconnect > now) continue;

        const char *port = entry.config.explicitPort.length()? entry.config.explicitPort.c_str() : entry.config.service.c_str();
        auto conn(network.BeginConnection(entry.config.host.c_str(), port));
        if(conn.first == nullptr) {
            ConnectionState(*entry.source, MapError(conn.second));
            // A failing connection is a very bad thing. It probably just makes sense to bail out but in case it's transient,
            // let's retry with a multiple of retry delay. It's really odd so give it quite some time to clear out.
            entry.nextReconnect = std::chrono::system_clock::now() + reconnectDelay * 4;
        }
        else {
            entry.route = conn.first;
            restarted++;
            entry.nextReconnect = zero;
            ConnectionState(*entry.source, ce_connecting);
        }
    }
}
