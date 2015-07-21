/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "M8MPoolConnectingApp.h"
#include "commands/Monitor/PoolStats.h"
#include <iostream>
#include <chrono>

class M8MPoolMonitoringApp : public M8MPoolConnectingApp,
                             protected commands::monitor::PoolStats::ValueSourceInterface {
public:
    M8MPoolMonitoringApp(NetworkInterface &factory) : M8MPoolConnectingApp(factory) { }

private:
    struct TimeLapsePoolStats : commands::monitor::PoolStats::ShareStats {
        std::chrono::time_point<std::chrono::system_clock> first;
        adouble acceptedDiff;
        const AbstractWorkSource *src = nullptr;

        explicit TimeLapsePoolStats() : acceptedDiff(0) { }
    };
    
    std::vector<TimeLapsePoolStats> poolShares;

    void StratumError(const AbstractWorkSource &owner, asizei i, int errorCode, const std::string &message);
    void ShareResponse(const AbstractWorkSource &me, asizei shareID, StratumShareResponse stat);
    void WorkerAuthorization(const AbstractWorkSource &owner, const std::string &worker, StratumState::AuthStatus status);
    void PoolCommand(const AbstractWorkSource &pool);
    void ConnectionState(const AbstractWorkSource &owner, ConnectionEvent ce);
    void BadHashes(const AbstractWorkSource &owner, asizei linearDevice, asizei badCount);
    void AddSent(const AbstractWorkSource &pool, asizei sent);

    static void ShareFeedback(const ShareIdentifier &share, const ShareFeedbackData &data, StratumShareResponse response, asizei poolIndex);
    static std::string Suffixed(unsigned __int64 value);
    /*! This is meant to be similar to legacy miners suffix_string_double function. SIMILAR. Not the same but hopefully compatible to parsers.
    The original is fairly complex in behaviour and it's pretty obvious that was not intended. This one works in an hopefully simplier way.
    It tries to encode a number in a string counting 5 chars.
    1) Numbers below 100 get as many digits after decimal separator as they can to fill so you get 4 digits.
    2) Numbers between 100 and 1000 produce integers as the deviation is considered acceptable. So you get 3 digits.
    3) When an iso suffix can be applied that will consider the highest three digits and work similarly but you'll only get 3 digits total
	    so, counting the separator they'll still be 5 chars unless the value is still big enough to not require decimals (2) so you'll get 4 chars total. */
    static std::string Suffixed(double value);
    
    // commands::monitor::PoolShares::ValueSourceInterface //////////////////////////////////////////////////
    bool GetPoolShareStats(commands::monitor::PoolStats::ShareStats &out, asizei poolIndex) {
        if(poolIndex >= poolShares.size()) return false;
        out = poolShares[poolIndex];
        return true;
    }
};
