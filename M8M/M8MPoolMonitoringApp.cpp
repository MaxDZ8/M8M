/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "M8MPoolMonitoringApp.h"


void M8MPoolMonitoringApp::StratumError(const AbstractWorkSource &owner, asizei i, int errorCode, const std::string &message) {
    using std::cout;
    cout<<"Pool ";
    if(owner.name.length()) cout<<'"'<<owner.name<<'"';
    else cout<<"0x"<<&owner;
    cout<<" reported stratum error ["<<std::to_string(i)<<']'<<std::endl;
    cout<<"    error code "<<std::dec<<errorCode<<"=0x"<<std::hex<<errorCode<<std::dec<<std::endl;
    cout<<"    \""<<message<<"\""<<std::endl;
}


void M8MPoolMonitoringApp::ShareResponse(const AbstractWorkSource &me, asizei shareID, StratumShareResponse stat) {
    ShareIdentifier key { &me, shareID };
    auto match(sentShares.find(key));
    if(match != sentShares.cend()) {
        ShareFeedback(key, match->second, stat, GetPoolIndex(me));
        for(auto &entry : poolShares) {
            if(&me != entry.src) continue;
            bool first = entry.accepted == 0 && entry.rejected == 0;
            if(stat == StratumShareResponse::ssr_rejected) entry.rejected++;
            else if(stat == StratumShareResponse::ssr_accepted) {
                entry.accepted++;
                entry.acceptedDiff += match->second.targetDiff;
            }
            entry.lastSubmitReply = std::chrono::system_clock::now();
            if(first) entry.first = entry.lastSubmitReply;
            auto lapse(entry.lastSubmitReply - entry.first);
            if(lapse.count()) {
                double secs = std::chrono::duration_cast<std::chrono::milliseconds>(lapse).count() / 1000.0;
                entry.daps = entry.acceptedDiff / secs;
            }
            break;
        }
        sentShares.erase(match);
    }
    else {
        using std::cout;
        cout<<"Pool ["<<GetPoolIndex(me)<<"] ";
        if(me.name.length()) cout<<'"'<<me.name<<"\" ";
        cout<<"signaled untracked share "<<shareID<<std::endl;
        // Maybe I should be throwing there.
    }
}


void M8MPoolMonitoringApp::WorkerAuthorization(const AbstractWorkSource &owner, const std::string &worker, StratumState::AuthStatus status) {
    if(status == StratumState::as_failed) {
        const std::wstring msg(L"A worker from pool[" + std::to_wstring(GetPoolIndex(owner)) + L"] failed to authenticate.");
        ChangeState(STATE_WARN);
        Popup(msg.c_str());
    }
    using std::cout;
    cout<<"Pool ";
    if(owner.name.length()) cout<<'"'<<owner.name<<'"';
    else cout<<"0x"<<&owner;
    cout<<" worker \""<<worker<<"\" ";
    switch(status) {
    case StratumState::as_pending: cout<<std::endl<<"  waiting for authorization."; break;
    case StratumState::as_accepted: cout<<std::endl<<"  authorized."; break;
    case StratumState::as_inferred: cout<<std::endl<<"  gets accepted shares anyway."; break;
    case StratumState::as_notRequired: cout<<std::endl<<"  seems to not need authorization."; break;
    case StratumState::as_off: cout<<" IMPOSSIBLE"; throw std::exception("Something has gone really wrong with worker authorization!"); break;
    case StratumState::as_failed: cout<<std::endl<<" !! FAILED AUTHORIZATION !!"; break;
    default:
        throw std::exception("Impossible. Code out of sync?");
    }
    cout<<std::endl;
}


void M8MPoolMonitoringApp::PoolCommand(const AbstractWorkSource &pool) {
    for(auto &update : poolShares) {
        if(update.src == &pool) {
            update.lastActivity = std::chrono::system_clock::now();
            break;
        }
    }
};


void M8MPoolMonitoringApp::ConnectionState(const AbstractWorkSource &owner, ConnectionEvent ce) {
    // Every time I try to pull up a new connection I take a chance at growing the stat vector.
    // In theory I could do this once by figuring a proper interface protocol but this makes it more flexible and easier.
    if(ce == ce_connecting) {
        poolShares.resize(GetNumServers());
        for(asizei cp = 0; cp < GetNumServers(); cp++) poolShares[cp].src = &GetPool(cp);
    }

    using std::cout;
    using namespace std::chrono;
    cout<<"Pool ";
    if(owner.name.length()) cout<<'"'<<owner.name<<'"';
    else cout<<"0x"<<&owner;
    cout<<' ';
    switch(ce) {
    case ce_connecting: {
        for(auto &entry : poolShares) {
            if(entry.src == &owner) {
                entry.numActivationAttempts++;
                break;
            }
        }
        cout<<"connecting."; break;
    }
    case ce_ready: {
        for(auto &entry : poolShares) {
            if(entry.src == &owner) {
                entry.lastActivated = system_clock::now();
                break;
            }
        }
        cout<<"connected."; break;
    }
    case ce_closing:
    case ce_failed:
        cout<<(ce == ce_closing? "shutting down." : "DISCONNECTED!");
        WorkChange(owner, std::unique_ptr<stratum::AbstractWorkFactory>());
        for(auto &entry : poolShares) {
            if(entry.src == &owner) {
                entry.cumulatedTime += system_clock::now() - entry.lastActivated;
                entry.lastActivated = system_clock::time_point();
                entry.lastConnDown = system_clock::now();
                break;
            }
        }
        break;
    case ce_failedResolve:
        cout<<" !! Error connecting, could not resolve URL !!";
        Warning(L"Failed to resolve a pool URL");
        break;
    case ce_badSocket:
        cout<<" !! Error connecting, could not create new socket !!";
        break;
    case ce_failedConnect:
        cout<<" !! Error connecting, could not initiate handshake !!";
        break;
    case ce_noRoutes:
        cout<<" !! Error connecting, no routes !!";
        break;
    default: std::cout<<"??Impossible! Code out of sync??";
    }
    cout<<std::endl;
}


void M8MPoolMonitoringApp::BadHashes(const AbstractWorkSource &owner, asizei linearDevice, asizei badCount) {
    std::cout<<"!!!! Device "<<linearDevice<<" produced "<<badCount<<" BAD HASH"<<(badCount > 1? "ES" : "");
    std::cout<<" processing pool["<<GetPoolIndex(owner)<<"] work !!!!"<<std::endl;
    //!< \todo also blink yellow here, perhaps stop mining if high percentage?
}


void M8MPoolMonitoringApp::AddSent(const AbstractWorkSource &pool, asizei sent) {
    poolShares[GetPoolIndex(pool)].sent += sent;
}


void M8MPoolMonitoringApp::ShareFeedback(const ShareIdentifier &share, const ShareFeedbackData &data, StratumShareResponse response, asizei poolIndex) {
    using namespace std;
    //   H   M   S   :       []<space>
    char timeString[2 + 2 + 2 + 2 + 1 + 3];
    auto sinceEpoch = time(NULL);
    tm now;
    gmtime_s(&now, &sinceEpoch);
    strftime(timeString, sizeof(timeString), "[%H:%M:%S] ", &now);

    char hashPart[9];
    const char *hex = "0123456789abcdef";
    for(asizei loop = 0; loop < 4; loop++) {
        hashPart[loop * 2 + 0] = hex[data.hashSlice[loop] >> 4];
        hashPart[loop * 2 + 1] = hex[data.hashSlice[loop] & 0x0F];
    }
    hashPart[8] = 0;

    stringstream diff;
    const std::string poolIdentifier(share.owner->name.length()? share.owner->name : ('[' + std::to_string(poolIndex) + ']'));
    if(response == ssr_expired) {
        cout<<"Share "<<share.shareIndex<<" sent to pool "<<poolIdentifier<<" has been dropped. No response from server."<<endl;
    }
    else {
        const char *reply = response == ssr_accepted? "Accepted" : "Rejected";
        cout<<timeString<<reply<<' '<<hashPart<<" Diff "<<Suffixed(data.shareDiff)<<'/'<<Suffixed(data.targetDiff);
        if(data.block) cout<<" BLOCK!";
        cout<<" GPU "<<data.gpuIndex<<std::endl;

    }
}


std::string M8MPoolMonitoringApp::Suffixed(unsigned __int64 value) {
    using namespace std;
    if(value < 1000) return to_string(value);
    const char *suffix = "KMGTPEZY";
    unsigned int part = value % 1000;
    value /= 1000;
    size_t iso = 0;
    while(iso < strlen(suffix) - 1 && value >= 1000) {
        part = value % 1000;
        value /= 1000;
        iso++;
    }
    auto ret(to_string(value));
    if(value < 100) {
        ret += '.';
        if(value > 10 || part >= 100) part /= 10;
        else if(part < 100) {
            ret += '0'; // if here, we divided at least once
            part /= 10; // and eat a digit away
        }
        ret += to_string(part / (value >= 10? 10 : 1));
    }
    return ret + suffix[iso];
}


std::string M8MPoolMonitoringApp::Suffixed(double value) {
    using namespace std;
    if(value < 100.0) {
        stringstream build;
        build.precision(3 - (value >= 10.0? 1 : 0));
        build<<fixed<<value;
        return build.str();
    }
    // In all other cases, we have sufficient digits to be accurate with integers.
    return Suffixed(static_cast<unsigned __int64>(value)); // truncate to be conservative
}
