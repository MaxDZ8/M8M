/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
//! \file Hubs of commands, where data is collected to be served to commands. Containers of commands and helper functions to populate them.
#include "commands/Monitor/ScanTime.h"
#include "commands/Monitor/DeviceShares.h"
#include "commands/Monitor/PoolShares.h"
#include "commands/Monitor/UptimeCMD.h"
#include "Connections.h"


struct TrackedValues : MiningPerformanceWatcherInterface, commands::monitor::DeviceShares::ValueSourceInterface, commands::monitor::PoolShares::ValueSourceInterface,
                       commands::monitor::UptimeCMD::StartTimeProvider {
    struct TimeLapseShareStats : commands::monitor::DeviceShares::ShareStats {
        std::chrono::time_point<std::chrono::system_clock> first;
        adouble totalDiff; //!< computing this on long time laps requires care... but fairly accurate up to 16 Mil values so let's take it easy for now.

        explicit TimeLapseShareStats() : totalDiff(0) { }
    };
    struct TimeLapsePoolStats : commands::monitor::PoolShares::ShareStats {
        std::chrono::time_point<std::chrono::system_clock> first;
        adouble acceptedDiff;
        AbstractWorkSource *src;

        explicit TimeLapsePoolStats() : acceptedDiff(0) { }
    };
    std::vector<TimeLapseShareStats> deviceShares;
    std::vector<TimeLapsePoolStats> poolShares;
    const Connections &servers;

    aulong prgStart;
    aulong minerStart;
    aulong firstNonce;
    const MiningPerformanceWatcherInterface *performance;

    TrackedValues(const Connections &src, aulong progStart)
        : servers(src), prgStart(progStart), minerStart(0), firstNonce(0), performance(nullptr) {
        poolShares.resize(servers.GetNumServers());
        for(asizei init = 0; init < poolShares.size(); init++) poolShares[init].src = &servers.GetServer(init);
    }

    bool GetDeviceShareStats(commands::monitor::DeviceShares::ShareStats &out, asizei dl) {
        if(dl < deviceShares.size()) out = deviceShares[dl];
        return dl < deviceShares.size();
    }
    
    bool GetPoolShareStats(commands::monitor::PoolShares::ShareStats &out, asizei pi) {
        if(pi < poolShares.size()) out = poolShares[pi];
        return pi < poolShares.size();
    }

    aulong GetStartTime(commands::monitor::UptimeCMD::StartTime st) {
        using namespace commands::monitor;
        switch(st) {
            case UptimeCMD::st_program: return prgStart;
            case UptimeCMD::st_hashing: return minerStart;
            case UptimeCMD::st_firstNonce: return firstNonce;
        }
        throw std::exception("GetStartTime, impossible, code out of sync?");
    }

    

    std::chrono::seconds GetAverageWindow() const {
        if(performance) return performance->GetAverageWindow();
        return std::chrono::seconds();
    }

    bool GetPerformance(DevStats &out, size_t device) const {
        if(performance) return performance->GetPerformance(out, device);
        return false;
    }

    asizei GetNumDevices() const {
        if(performance) return performance->GetNumDevices();
        return 0;
    }
};



#include "commands/Admin/ConfigFileCMD.h"
#include "commands/Admin/GetRawConfigCMD.h"

struct CFGLoadInfo {
    std::wstring configFile, configDir;
    bool specified, redirected, valid;

    explicit CFGLoadInfo() : specified(false), redirected(false), valid(false) { }
};

struct TrackedAdminValues : public commands::admin::ConfigFileCMD::ConfigInfoProviderInterface {
    CFGLoadInfo loadInfo;

    commands::admin::RawConfig loadedConfig;

    asizei reloadRequested;
    bool willReloadListening;
    bool customConfDir;

    TrackedAdminValues() : reloadRequested(0), willReloadListening(false) { }

    std::wstring Filename() const { return loadInfo.configFile; }
    bool CustomConfDir() const { return customConfDir; }
    bool Explicit() const { return loadInfo.specified; }
    bool Redirected() const { return loadInfo.redirected; }
    bool Valid() const { return loadInfo.valid; }
};


typedef std::vector< std::unique_ptr<commands::AbstractCommand> > WebCommands;


#include "WebMonitorTracker.h"


template<typename CreateClass, typename Param>
void SimpleCommand(WebCommands &persist, WebTrackerOnOffConn &mon, Param &param) {
    std::unique_ptr<CreateClass> build(new CreateClass(param));
    mon.RegisterCommand(*build);
    persist.push_back(std::move(build));
}


#include "commands/Monitor/SystemInfoCMD.h"
#include "commands/Monitor/AlgoCMD.h"
#include "commands/Monitor/PoolCMD.h"
#include "commands/Monitor/DeviceConfigCMD.h"
#include "commands/Monitor/RejectReasonCMD.h"
#include "commands/Monitor/ConfigInfoCMD.h"
#include "commands/ExtensionListCMD.h"
#include "commands/UnsubscribeCMD.h"
#include "commands/UpgradeCMD.h"
#include "commands/VersionCMD.h"



template<typename MiningProcessorsProvider, typename WebSocketService>
void RegisterMonitorCommands(WebCommands &persist, WebSocketService &mon, MiningProcessorsProvider &procs, const Connections &connections, TrackedValues &tracking, const std::pair<AlgoIdentifier, aulong> &algo, const std::vector<auint> &devConfMap, const std::vector< std::vector<MinerSupport::ConfReasons> > &rejectReasons, commands::monitor::ConfigInfoCMD::ConfigDesc &configDesc) {
    using namespace commands::monitor;
    SimpleCommand< SystemInfoCMD<MiningProcessorsProvider> >(persist, mon, procs);
    {
        std::unique_ptr<AlgoCMD> build(new AlgoCMD(algo.first, algo.second));
        mon.RegisterCommand(*build);
        persist.push_back(std::move(build));
    }
    SimpleCommand<PoolCMD>(persist, mon, connections);
    SimpleCommand<DeviceConfigCMD>(persist, mon, devConfMap);
    SimpleCommand<RejectReasonCMD>(persist, mon, rejectReasons);
    SimpleCommand<ConfigInfoCMD>(persist, mon, configDesc);
    SimpleCommand<ScanTime>(persist, mon, tracking);
    SimpleCommand<DeviceShares>(persist, mon, tracking);
    SimpleCommand<PoolShares>(persist, mon, tracking);
    SimpleCommand<UptimeCMD>(persist, mon, tracking);
    {
        std::unique_ptr<commands::VersionCMD> build(new commands::VersionCMD());
        mon.RegisterCommand(*build);
        persist.push_back(std::move(build));
    }
    SimpleCommand<commands::ExtensionListCMD>(persist, mon, mon.extensions);
    SimpleCommand<commands::UpgradeCMD>(persist, mon, mon.extensions);
    SimpleCommand<commands::UnsubscribeCMD>(persist, mon, mon);
}


#include "commands/Admin/SaveRawConfigCMD.h"
#include "commands/Admin/ReloadCMD.h"


void RegisterAdminCommands(WebCommands &persist, WebAdminTracker &mon, TrackedAdminValues &tracking) {
    using namespace commands::admin;
    SimpleCommand<ConfigFileCMD>(persist, mon, tracking);
    SimpleCommand<GetRawConfigCMD>(persist, mon, tracking.loadedConfig);
    {
        std::unique_ptr<SaveRawConfigCMD> build(new SaveRawConfigCMD(tracking.loadInfo.configDir.c_str(), tracking.loadInfo.configFile.c_str()));
        mon.RegisterCommand(*build);
        persist.push_back(std::move(build));
    }
    SimpleCommand<ReloadCMD>(persist, mon, [&tracking]() {
        tracking.reloadRequested = 1;
        return tracking.willReloadListening;
    });
}
