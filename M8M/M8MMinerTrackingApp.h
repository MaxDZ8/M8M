/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "M8MMiningApp.h"
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>
#include "MiningPerformanceWatcher.h"
#include "commands/Monitor/DeviceShares.h"
#include "commands/Monitor/ScanTime.h"
#include "commands/Monitor/RejectReasonCMD.h"

class M8MMinerTrackingApp : public M8MMiningApp,
                            protected commands::monitor::DeviceShares::ValueSourceInterface,
                            protected commands::monitor::RejectReasonCMD::RejInfoProviderInterface {
public:
    M8MMinerTrackingApp(NetworkInterface &factory) : M8MMiningApp(factory) { }

    void AddDeviceReject(asizei config, std::string &utf8, asizei devIndex) {
        devRejects.resize(GetNumDevices());
        perfStats.SetNumDevices(devRejects.size());
        auto put(std::find_if(devRejects[devIndex].begin(), devRejects[devIndex].end(), [config](const DeviceRejection &test) {
            return test.confIndex == config;
        }));
        if(put == devRejects[devIndex].end()) {
            devRejects[devIndex].push_back(DeviceRejection());
            put = devRejects[devIndex].begin() + devRejects[devIndex].size() - 1;
            put->confIndex = auint(config); // how many configs can I really have?
        }
        put->reasons.push_back(std::move(utf8));
    }

    /*! Performance monitoring callback, called asynchronously by the miner thread(s). */
    void IterationCompleted(asizei devIndex, bool found, std::chrono::microseconds elapsed) {
        perfStats.Completed(devIndex, found, elapsed);
    }

    /*! Stuff returned from a mining device. Validated but potentially stale. Not sent to pool yet! */
    void UpdateDeviceStats(const VerifiedNonces &found) {
        deviceShares.resize(GetNumDevices());
        auto &dst(deviceShares[found.device]);
        dst.found += found.Total();
        dst.bad += found.wrong;
        dst.discarded += found.discarded;
        dst.last = std::chrono::system_clock::now();
        if(dst.found == found.Total()) dst.first = dst.last;
        // Stale not updated here but rather from SendResults
        //dst.stale += ...
        for(auto &res : found.nonces) dst.totalDiff += res.diff;
        auto lapse(dst.last - dst.first);
        if(lapse.count()) {
            double secs = std::chrono::duration_cast<std::chrono::milliseconds>(lapse).count() / 1000.0;
            dst.dsps = dst.totalDiff / secs;
        }
    }

    void AddStale(asizei linDevice, asizei count) {
        deviceShares[linDevice].stale += count;
        deviceShares[linDevice].last = std::chrono::system_clock::now();
    }

protected:
    struct DeviceRejection {
        auint confIndex;
        std::vector<std::string> reasons;
    };
    /*! An entry for each device in linear index order, why config[configIndex] was not mapped to device [i]?
    Goes mostly to "rejectReason" command. */
    std::vector<std::vector<DeviceRejection>> devRejects;

    SyncMiningPerformanceWatcher perfStats;

    struct TimeLapseShareStats : commands::monitor::DeviceShares::ShareStats {
        std::chrono::time_point<std::chrono::system_clock> first;
        adouble totalDiff; //!< computing this on long time laps requires care... but fairly accurate up to 16 Mil values so let's take it easy for now.

        explicit TimeLapseShareStats() : totalDiff(0) { }
    };
    std::vector<TimeLapseShareStats> deviceShares;
private:
    // commands::monitor::DeviceShares::ValueSourceInterface ////////////////////////////////////////////////
    bool GetDeviceShareStats(commands::monitor::DeviceShares::ShareStats &out, asizei devLinearIndex) {
        // this must be generated before probing, if the web monitor is started before UpdateDeviceStats is called it will think we have 0 devices.
        deviceShares.resize(GetNumDevices());
        if(devLinearIndex >= deviceShares.size()) return false;
        out = deviceShares[devLinearIndex];
        return true;
    }

    // commands::monitor::RejectReasonCMD::RejInfoProviderInterface /////////////////////////////////////////
    asizei GetNumEntries() const { return devRejects.size(); }
    asizei GetNumRejectedConfigs(asizei dev) const { return devRejects[dev].size(); }
    std::vector<std::string> GetRejectionReasons(asizei dev, asizei entry) const { return devRejects[dev][entry].reasons; }
    auint GetRejectedConfigIndex(asizei dev, asizei entry) const { return devRejects[dev][entry].confIndex; }
};
