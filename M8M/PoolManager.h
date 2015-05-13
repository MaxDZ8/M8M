/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/WorkSource.h"
#include "NonceStructs.h"
#include "commands/Monitor/PoolCMD.h"
#include <chrono>


/*! This was originally called "Connections" and supposed to be a simple container of objects derived from AbstractWorkSource.
It then got some extra helper functions and got some nasty interactions with the monitoring machinery.

This has been partially rewritten when I realized AbstractWorkSource objects were really supposed to be much more focused.
This could now be called "AbstractWorkSourceSet" but for simplicity, it isn't.

The bottom line is that you populate this with all the pools from the configuration file. They are all kept here.
Then, this object is instructed about which algo to mine. Eligible pools are internally selected and their connection initiated.
When connection is estabilished, pools will initiate stratum handshake automatically; the outer code can assume AbstractWorkSource pointers
will stay persistent as long as this object exist albeit they might be active or not. */
class PoolManager : public commands::monitor::PoolCMD::PoolEnumeratorInterface {
public:
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
	std::function<void(const AbstractWorkSource &pool, std::unique_ptr<stratum::AbstractWorkFactory> &newWork)> dispatchFunc;
    std::function<void(const AbstractWorkSource &pool, stratum::WorkDiff &diff)> diffChangeFunc;
    std::function<void(const AbstractWorkSource &pool, ConnectionEvent)> connStateFunc;
    std::function<void(const AbstractWorkSource &pool, asizei index, int errorCode, const std::string &message)> stratErrFunc;
    std::function<void(const AbstractWorkSource &pool)> onPoolCommand; //!< if provided, call this every time a certain pool receives something terminated with newline, (maybe not a command in protocol sense)

    // Those are in order of use. Pretty self-explicatory.
    PoolManager(Network &factory) : network(factory) { }
    void AddPools(const std::vector< std::unique_ptr<PoolInfo> > &all); //!< Call this multiple times to add all the pools. This generates persistent state.
    void InitPools(const AbstractWorkSource::AlgoInfo &info); //!< Then call this once per algorithm. This will generate the pool objects which will be used by the mining process
    asizei Activate(const std::string &algo); //!< Then call this to disable all active pools and start going with the specified ones. \returns number of activated pools
	void FillSleepLists(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);
    void Refresh(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);
	virtual ~PoolManager();

    /*! The new value will apply only to connections going down AFTER this is called.
    However, already planned to be updated pools will get their wake up time adjusted.
    This does not wake up pools. Only Refresh attempts to bring pools back online. */
    void SetReconnectDelay(std::chrono::seconds delay);

    //! Used to build stats in TrackedValues
    AbstractWorkSource& GetServer(asizei i) const { return *pools[i].source; }

    // PoolEnumeratorInterface - - - - - - - - - - - -
    asizei GetNumServers() const { return pools.size(); }
    const PoolInfo& GetServerInfo(asizei i) const { return pools[i].config; }
    std::string GetConnectionURL(asizei i) const {
        const char *port = pools[i].config.explicitPort.length()? pools[i].config.explicitPort.c_str() : pools[i].config.service.c_str();
        return pools[i].config.host + ':' + port;
    }
    std::vector< std::pair<const char*, StratumState::AuthStatus> > GetWorkerAuthState(asizei i) const;

private:
	Network &network;
    std::chrono::seconds reconnDelay = std::chrono::seconds(120);

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

    asizei AttemptReconnections();
    static ConnectionEvent MapError(NetworkInterface::ConnectionError ce);
};
