/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/Network.h"
#include "M8MConfiguredApp.h"
#include "commands/Monitor/PoolCMD.h"
#include "../Common/WorkSource.h"
#include "NonceStructs.h"

/*! Managing pool connections was originally part of the "Connections" object, later renamed "PoolManager".
The main problem with the pool manager is that pretty much everybody needed it for what it did or perhaps just
to access it as a pool enumerator by web commands.
It still didn't provide enough abstraction to make main() easy to understand as it lacked application-specific facilities
such as the callbacks which did go in main() with quite some verbosity.
The pool manager is still pretty good and I could have used it as a base class/component but I didn't. Why?
The main problem is that pools have (had) a serious quirk: they needed an algorithm to be instantiated to be themselves instantiated.
So I'm taking the chance to refactor a bit more extensively and get the rid of that shit. */
class M8MPoolConnectingApp : public M8MConfiguredApp, protected commands::monitor::PoolCMD::PoolEnumeratorInterface {
public:
    M8MPoolConnectingApp(NetworkInterface &factory) : network(factory) { }
    /*! Takes ownership of pointer and generates persistent state about the pool.
    \returns False if pool uses an unknown algorithm. */
    bool AddPool(const PoolInfo &copy, const CanonicalInfo &algoInfo);

    /*! Activating a pool involves pulling up a TCP connection to it. It's a non-blocking operation.
    After the TCP connection comes up, a proper stratum object will be created and handshake will take place.
    At some point the pool(s) will be able to generate work.
    \returns Number of pools attempting activation. If this is zero, something is definitely wrong in config. */
    asizei BeginPoolActivation(const char *algo);

    void SetReconnectDelay(std::chrono::seconds retry) { reconnectDelay = retry; }

	virtual void FillSleepLists(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);
    virtual void Refresh(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);

protected:
    NetworkInterface &network;

    //! Most of the time, this is called before ShareResponse, signaling reject.
    virtual void StratumError(const AbstractWorkSource &owner, asizei i, int errorCode, const std::string &message) = 0;

    //! Ideally called when a worksource replies to a "submit" share. Or if too much time goes by before receiving such reply.
    virtual void ShareResponse(const AbstractWorkSource &me, asizei shareID, StratumShareResponse stat) = 0;
    virtual void WorkerAuthorization(const AbstractWorkSource &owner, const std::string &worker, StratumState::AuthStatus status) = 0;
    virtual void PoolCommand(const AbstractWorkSource &owner) = 0; //!< called when at least a byte is received. Or a command? I haven't decided yet.
	virtual void WorkChange(const AbstractWorkSource &pool, std::unique_ptr<stratum::AbstractWorkFactory> &newWork) = 0; //!< newWork can be nullptr(no work)
    virtual void DiffChange(const AbstractWorkSource &pool, const stratum::WorkDiff &diff) = 0;

    //! Performance monitoring. Called after some shares have been queued for sending to the pool.
    virtual void AddSent(const AbstractWorkSource &pool, asizei sent) = 0;

    //! Performance monitoring. A certain device took too much to scan hashes its work became stale. It happens.
    virtual void AddStale(asizei linearDevice, asizei staleCount) = 0;

    enum ConnectionEvent {
        ce_connecting,
        ce_ready,
        ce_closing,
        ce_failed,

        // Those are generated when the pool socket attempts connection. See NetworkInterface::ConnectionError
        ce_failedResolve,
        ce_badSocket,
        ce_failedConnect,
        ce_noRoutes
    };
    virtual void ConnectionState(const AbstractWorkSource &pool, ConnectionEvent status) = 0;

    auto MapError(NetworkInterface::ConnectionError ce) -> ConnectionEvent {
        switch(ce) {
        case NetworkInterface::ce_failedResolve: return ce_failedResolve;
        case NetworkInterface::ce_badSocket: return ce_badSocket;
        case NetworkInterface::ce_failedConnect: return ce_failedConnect;
        case NetworkInterface::ce_noRoutes: return ce_noRoutes;
        }
        throw std::exception("Something went very wrong with connection event mappings, code out of sync");
    }

    asizei GetPoolIndex(const AbstractWorkSource &existing) const {
        asizei index = 0;
        while(&existing != pools[index].source.get()) index++;
        return index;
    }

    const AbstractWorkSource& GetPool(asizei index) const { return *pools[index].source; }

    asizei GetNumActiveServers() const {
        return std::count_if(pools.cbegin(), pools.cend(), [](const Pool &test) {
            return test.source && test.source->Ready();
        });
    }

    struct ShareIdentifier {
        const AbstractWorkSource *owner;
        asizei shareIndex;

        bool operator<(const ShareIdentifier &other) const {
            const ptrdiff_t diff = owner - other.owner; // stable as long as AbstractWorkSource objects are persistent, which is the case here
            return diff < ptrdiff_t(0) || shareIndex < other.shareIndex;
        }
    };

    struct ShareFeedbackData {
        adouble shareDiff, targetDiff;
        bool block;
        asizei gpuIndex;
        std::array<unsigned char, 4> hashSlice;
    };
    std::map<ShareIdentifier, ShareFeedbackData> sentShares;

    void SendResults(const NonceOriginIdentifier &from, const VerifiedNonces &sharesFound);

    virtual void BadHashes(const AbstractWorkSource &owner, asizei linDevice, asizei badCount) = 0;

    // PoolEnumeratorInterface ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    asizei GetNumServers() const { return pools.size(); }
    const PoolInfo& GetServerInfo(asizei i) const { return pools[i].config; }
    std::string GetConnectionURL(asizei i) const {
        const char *port = pools[i].config.explicitPort.length()? pools[i].config.explicitPort.c_str() : pools[i].config.service.c_str();
        return pools[i].config.host + ':' + port;
    }
    std::vector< std::pair<const char*, StratumState::AuthStatus> > GetWorkerAuthState(asizei i) const {
        std::vector< std::pair<const char*, StratumState::AuthStatus> > ret;
        pools[i].source->GetUserNames(ret);
        return ret;
    }

private:
    struct Pool {
        PoolInfo config;
        std::chrono::system_clock::time_point activated; //!< when the pool was activated last, 0 when disabled
        asizei numActivations = 0;
        std::chrono::system_clock::duration totalTime; //!< When a pool is deactivated this gets incremented. Watch out to fix active pools.
        std::chrono::system_clock::time_point nextReconnect; //!< mostly 0. Set to a future time point when a connection goes down.

        std::unique_ptr<WorkSource> source; //!< This is created when InitPools for the corresponding algo is called and then stays persistent.
        Network::ConnectedSocketInterface *route = nullptr; //!< Created when Activate is called
        explicit Pool() = default;
        Pool(Pool &&other) {
            config = std::move(other.config);
            activated = std::move(other.activated);
            numActivations = other.numActivations;
            totalTime = std::move(totalTime);
            source = std::move(other.source);
            route = other.route;
            other.route = nullptr;
        }
    private:
        Pool(const Pool &) = delete;
    };
    std::vector<Pool> pools;
    std::chrono::seconds reconnectDelay = std::chrono::seconds(30);
    void AttemptReconnections();
};
