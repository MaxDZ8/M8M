/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
/* Project M8M
MassimoDZ8 Miner or, more hopefully MightyMiner.
Hopefully also the 1st multi-phase miner.
Multiphasing means moving beyond just dispatching kernels. In my opinion that's what
"3rd gen" miners should do.
Initially intended as a Scrypt miner based on GPGPU constructs to reduce memory
conflicts to pad gather phase. */
#include <string>
#include <codecvt>
#include <iostream>
#include "PoolManager.h"
#include "ProcessingNodesFactory.h"
#include "AlgoMiner.h"
#include <iomanip>
#include "M8MIcon.h"
#include "../Common/AREN/SharedUtils/AutoConsole.h"
#include "cmdHubs.h"
#include "StartParams.h"
#include "mainHelpers.h"

#include "../Common/AREN/SharedUtils/OSUniqueChecker.h"

#if defined _WIN32
#include <ShlObj.h>
#include <KnownFolders.h>
#endif

#include "../Common/AREN/SharedUtils/dirControl.h"



void _stdcall ErrorsToSTDOUT(const char *err, const void *priv, size_t privSz, void *userData) {
    using std::cout;
    using std::endl;
	cout<<"ERROR reported \""<<err<<"\"";
	if(priv && privSz) {
		cout<<endl;
		const char *oct = reinterpret_cast<const char*>(priv);
		for(asizei loop = 0; loop < privSz; loop++) cout<<oct[loop];
	}
	cout<<endl;
	cout<<"userdata is "<<(userData? "non-" : "")<<"null";
	cout<<endl;
	cout.flush();
}



#if defined(_DEBUG)
#define TIMEOUT_MS ( 60 * 1000 * 30)
#else
#define TIMEOUT_MS (120 * 1000)
#endif

/*! The IO thread also wakes up every once in a while to check if there are shares to be sent.
In the future I might want to switch this to signal-based, but for the time being, I'm preferring ease of implementation.
The main problem is that waking it up on signal would require a socket to sleep on at network level.
Windows allows to sleep on sockets and thread signals but I'm not sure other OSs allow this. */
#define POLL_PERIOD_MS 200


Settings* LoadSettings(commands::admin::RawConfig &loadAttempt, CFGLoadInfo &loadInfo) {
    unique_ptr<Settings> configuration(std::make_unique<Settings>());

	using namespace rapidjson;
	CFGLoadErrInfo failure;
    loadInfo.specified = true;
    if(loadInfo.configFile.empty()) {
        loadInfo.specified = false;
        loadInfo.configFile = L"init.json";
    }
	const std::wstring ori(loadInfo.configFile);
	asizei attempt = loadInfo.specified? 1 : 0;
	while(attempt < 2) {
        if(!LoadConfigJSON(loadAttempt.good, failure, loadInfo.configFile)) {
            configuration.reset();
            break;
        }
		if(loadAttempt.good.IsObject() == false) {
			loadAttempt.good.SetNull();
			loadAttempt.errCode = failure.errCode;
			loadAttempt.errOff = failure.errOff;
			loadAttempt.errDesc = std::move(failure.errDesc);
			loadAttempt.raw = failure.raw.get();
            configuration.reset();
			break;
		}
		else {
			loadAttempt.valueErrors.clear();
			const Value::ConstMemberIterator redirect = loadAttempt.good.FindMember("userConfiguration");
			if(attempt || redirect == loadAttempt.good.MemberEnd() || redirect->value.IsString() == false) {
				configuration.reset(BuildSettings(loadAttempt.valueErrors, loadAttempt.good));
				break;
			}
			else {
				std::string utfbyte(redirect->value.GetString(), redirect->value.GetStringLength());
				std::wstring_convert< std::codecvt_utf8_utf16<wchar_t> > convert;
				loadInfo.configFile = convert.from_bytes(utfbyte);
			}
		}
		attempt++;
	}
	loadInfo.redirected = ori != loadInfo.configFile;
	loadInfo.valid = configuration != nullptr;
    return configuration.release();
}


//! This silly object is really meant to just provide a way to build a message pump without resolving to a function having a 6+ parameters.
struct MinerMessagePump {
    AbstractNotifyIcon &notify;
    AbstractIconCompositer &iconBitmaps;
    Network &network;
    PoolManager &remote;
    TrackedValues &stats;

    NonceFindersInterface *miner = nullptr;

    MinerMessagePump(AbstractNotifyIcon &icon, AbstractIconCompositer &rasters, Network &net, PoolManager &servers, TrackedValues &track)
        : notify(icon), iconBitmaps(rasters), network(net), remote(servers), stats(track) { }

    bool Pump(const std::function<void(auint ms)> &sleepFunc, bool &run, MiniServers &web, aulong &firstNonce, std::map<ShareIdentifier, ShareFeedbackData> &sentShares, TrackedAdminValues &admin) {
		bool firstShare = true;
		asizei sinceActivity = 0;
		std::vector<Network::SocketInterface*> toRead, toWrite;
        std::wstring persistentError;
        std::chrono::system_clock::time_point lastPopup;
		while(run) {
			if(admin.reloadRequested) {
				if(web.Nobody()) break;
				if(admin.reloadRequested == 1) {
					web.monitor.BeginClose();
					web.admin.BeginClose();
				}
				admin.reloadRequested++;
			}
			notify.Tick();
			toRead.clear();
			toWrite.clear();
			remote.FillSleepLists(toRead, toWrite);
			web.monitor.FillSleepLists(toRead, toWrite);
			web.admin.FillSleepLists(toRead, toWrite);
			asizei updated = 0;
			if(toRead.size() || toWrite.size()) { // typically 0 or 2 is true, 0 happens if no cfg loaded
				updated = network.SleepOn(toRead, toWrite, POLL_PERIOD_MS);
			}
			else sleepFunc(POLL_PERIOD_MS); //!< \todo perhaps I should leave this on and let this CPU gobble up resources so it can be signaled?
			remote.Refresh(toRead, toWrite); // even when !updated, might attempt to reconnect
			web.monitor.Refresh(toRead, toWrite); // might complete shutdown
			web.admin.Refresh(toRead, toWrite);
            if(!updated) {
				if(toRead.size()) sinceActivity += POLL_PERIOD_MS;
				if(sinceActivity >= TIMEOUT_MS && web.Nobody()) {
                    persistentError = L"No network activity in " + std::to_wstring(TIMEOUT_MS / 1000) + L" seconds.";
                    std::vector<aubyte> ico;
			        iconBitmaps.SetCurrentState(STATE_ERROR);
			        iconBitmaps.GetCompositedIcon(ico);
			        notify.SetIcon(ico.data(), M8M_ICON_SIZE, M8M_ICON_SIZE);
				}
			}
			else sinceActivity = 0;

            if(persistentError.size()) {
                using namespace std::chrono;
                auto now(system_clock::now());
                auto sinceLast = duration_cast<std::chrono::seconds>(now - lastPopup);
                if(sinceLast > seconds(5 * 60)) {
                    notify.ShowMessage(persistentError.c_str());
                    lastPopup = system_clock::now();
                }
            }
            WatchDog(miner);
            
			std::string errorDesc;
            NonceOriginIdentifier from;
			VerifiedNonces sharesFound;
			if(miner && miner->ResultsFound(from, sharesFound)) {
				if(firstShare) {
					firstShare = false;
                    std::wstring msg(L"Found my first result!\n");
                    if(sharesFound.wrong) msg = L"GPU produced bad numbers.\nSomething is very wrong!"; //!< \todo blink yellow for a few seconds every time a result is wrong
                    else msg += L"Numbers are getting crunched as expected.";
					notify.ShowMessage(msg.c_str());
                    using namespace std::chrono;
                    firstNonce = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
				}
                UpdateDeviceStats(sharesFound);
                SendResults(from, sharesFound, sentShares);
			}
        }
	    return admin.reloadRequested != 0;
    }

private:
    NonceFindersInterface::Status minerState = NonceFindersInterface::s_created;

    //! Refresh miner icon according to state but with some care.
    void WatchDog(NonceFindersInterface *miner) {
        if(!miner) return; // there's another set error, keep that one.

        const auto state = miner->TestStatus();
        if(minerState == state) return;

	    std::vector<aubyte> ico;
        switch(state) {
        case NonceFindersInterface::s_initialized:
        case NonceFindersInterface::s_unresponsive: {
			iconBitmaps.SetCurrentState(STATE_WARN);
            if(state == NonceFindersInterface::s_unresponsive) notify.ShowMessage(L"Mining process is having issues.");
        } break;
        case NonceFindersInterface::s_stopped:
        case NonceFindersInterface::s_initFailed:
        case NonceFindersInterface::s_terminated: {
			iconBitmaps.SetCurrentState(STATE_ERROR);
            if(state == NonceFindersInterface::s_initFailed) notify.ShowMessage(L"Could not initialize miner.");
            if(state == NonceFindersInterface::s_terminated) {
                auto detail(miner->GetTerminationReason());
                if(detail.empty()) notify.ShowMessage(L"Miner process terminated!\nThis is very wrong!");
                else {
                    std::vector<wchar_t> meh(detail.size() + 1);
                    for(asizei cp = 0; cp < detail.size(); cp++) meh[cp] = detail[cp];
                    meh.back() = 0;
                    notify.ShowMessage(meh.data());
                }
            }
        } break;
        case NonceFindersInterface::s_running: {
			iconBitmaps.SetCurrentState(STATE_OK);
        } break;
        default: throw "Impossible, missing a transition? Code incoherent.";
        }
		iconBitmaps.GetCompositedIcon(ico);
		notify.SetIcon(ico.data(), M8M_ICON_SIZE, M8M_ICON_SIZE);
        minerState = state;
    }

    void SendResults(const NonceOriginIdentifier &from, const VerifiedNonces &sharesFound, std::map<ShareIdentifier, ShareFeedbackData> &sentShares) {
        AbstractWorkSource *owner = nullptr;
        asizei poolIndex = 0;
        for(asizei search = 0; search < remote.GetNumServers(); search++) {
            auto candidate = &remote.GetServer(search);
            if(candidate == from.owner) {
                owner = candidate;
                poolIndex = search;
                break;
            }
        }
        if(!owner) throw "Impossible, WU owner not found"; // really wrong stuff. Most likely a bug in code or possibly we just got hit by some cosmic ray
	    //std::cout<<"Device "<<sharesFound.device<<" found "<<sharesFound.Total()<<" nonce"<<(sharesFound.Total()>1? "s" : "")
        //            <<'('<<sharesFound.discarded<<" below real target)"<<std::endl;
        if(sharesFound.wrong) std::cout<<"!!!! "<<sharesFound.wrong<<" WRONG !!!!"<<std::endl;  //!< \todo also blink yellow here, perhaps stop mining if high percentage?
	    std::cout.setf(std::ios::dec);
        auto ntime = owner->IsCurrentJob(from.job);
        if(ntime) {
            for(auto &result : sharesFound.nonces) {
                ShareIdentifier shareSrc;
                shareSrc.owner = owner;
                shareSrc.poolIndex = poolIndex;
                shareSrc.shareIndex = owner->SendShare(from.job, ntime, sharesFound.nonce2, result.nonce);

                ShareFeedbackData fback;
                fback.block = result.block;
                fback.hashSlice = result.hashSlice;
                fback.shareDiff = result.diff;
                fback.targetDiff = sharesFound.targetDiff;
                fback.gpuIndex = sharesFound.device;
                        
                sentShares.insert(std::make_pair(shareSrc, fback));
                
                for(auto &entry : stats.poolShares) {
                    if(owner != entry.src) continue;
                    entry.sent++; // note I count replies, not sends
                    break;
                }
            }
        }
        else stats.deviceShares[sharesFound.device].stale += sharesFound.nonces.size();
    }

    

    void UpdateDeviceStats(const VerifiedNonces &results) {
        auto &dst(stats.deviceShares[results.device]);
        dst.found += results.Total();
        dst.bad += results.wrong;
        dst.discarded += results.discarded;
        dst.last = std::chrono::system_clock::now();
        if(dst.found == results.Total()) dst.first = dst.last;
        // Stale not updated here but rather in SendResults
        //dst.stale += ...
        for(auto &res : results.nonces) dst.totalDiff += res.diff;
        auto lapse(dst.last - dst.first);
        if(lapse.count()) {
            double secs = std::chrono::duration_cast<std::chrono::milliseconds>(lapse).count() / 1000.0;
            dst.dsps = dst.totalDiff / secs;
        }
    }
};


#if defined(_WIN32)
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE unusedLegacyW16, PWSTR cmdLine, int showStatus) {
#else
int main(int argc, char **argv) {
#endif

    StartParams cmdParams(cmdLine);
    std::unique_ptr<sharedUtils::system::AutoConsole<false>> handyOutputForDebugging;
    std::wstring configFile;
    bool configSpecified = false;
    bool invisible = false;
    bool fail = false;
    std::unique_ptr<AbstractIconCompositer> iconBitmaps;
    auto fatal = [&fail](const wchar_t *msg) {
        MessageBox(NULL, msg, L"Fatal error!", MB_ICONERROR | MB_SYSTEMMODAL | MB_SETFOREGROUND);
        fail = true;
    };
    auto fatalAscii = [&fatal](const char *msg) {
        std::vector<wchar_t> unicode(strlen(msg) + 1); // not converted correctly, I could/should use rapidjson here but is it worth?
        for(asizei cp = 0; msg[cp]; cp++) unicode[cp] = msg[cp];
        unicode[strlen(msg)] = 0;
        fatal(unicode.data());
    };

    try { // mangling parameters
        std::vector<wchar_t> value;
        if(cmdParams.ConsumeParam(value, L"alreadyRunning")) {
            if(value.size()) {
                const wchar_t *title = L"--alreadyRunning";
                const wchar_t *msg = L"Looks like you've specified a value for a parameter which is not supposed to take any."
                                     L"\nExecution will continue but clean up your command line please.";
                MessageBox(NULL, msg, title, MB_OK | MB_ICONWARNING);
            }
        }
        else {
	        /* M8M is super spiffy and so minimalistic I often forgot it's already running.
	        Running multiple instances might make sense in the future (for example to mine different algos on different cards)
	        but it's not supported for the time being. Having multiple M8M instances doing the same thing will only cause driver work and GPU I$ to work extra hard. */
            OSUniqueChecker onlyOne;
	        if(onlyOne.CanStart(L"M8M_unique_instance_systemwide_mutex") == false) {
		        const wchar_t *msg = L"It seems you forgot M8M is already running. Check out your notification area!\n"
				                        L"Running multiple instances is not a good idea in general and it's not currently supported.\n"
								        L"This program will now close.";
		        const wchar_t *title = L"Already running!";
            #if defined(_WIN32)
		        MessageBox(NULL, msg, title, MB_ICONINFORMATION | MB_SYSTEMMODAL | MB_SETFOREGROUND);
            #else
            #error Whoops! Tell the user to not do that!
            #endif
		        return 0;
            }
        }
        if(cmdParams.ConsumeParam(value, L"console")) {
            if(_wcsicmp(L"new", value.data()) == 0) handyOutputForDebugging = std::make_unique<sharedUtils::system::AutoConsole<false>>();
#if defined(_WIN32)
            else if(_wcsicmp(L"parent", value.data()) == 0) handyOutputForDebugging = std::make_unique<sharedUtils::system::AutoConsole<false>>(auint(~0));
#endif
            else {
                auto number(_wtoll(value.data()));
                if(number < 0 || number > auint(~0)) throw std::exception("Invalid value for parameter --console");
                handyOutputForDebugging = std::make_unique<sharedUtils::system::AutoConsole<false>>(auint(number));
            }
	        handyOutputForDebugging->Enable();
        }
        if(cmdParams.ConsumeParam(value, L"config")) {
            configFile = value.data();
            configSpecified = true;
        }
        else configFile = L"init.json";
        if(cmdParams.ConsumeParam(value, L"invisible")) {
            if(value.size()) {
                const wchar_t *title = L"--invisible";
                const wchar_t *msg = L"Looks like you've specified a value for a parameter which is not supposed to take any."
                                     L"\nExecution will continue but clean up your command line please.";
                MessageBox(NULL, msg, title, MB_OK | MB_ICONWARNING);
            }
            invisible = true;
            iconBitmaps.reset(new DummyIconCompositer(M8M_ICON_SIZE, M8M_ICON_SIZE));
        }
        else iconBitmaps.reset(new IconCompositer(M8M_ICON_SIZE, M8M_ICON_SIZE));

        if(cmdParams.FullyConsumed() == false) {
            const wchar_t *title = L"Dirty command line";
            const wchar_t *msg = L"Command line parameters couldn't consume everything you wrote."
                                 L"\nExecution will continue but clean up your command line please.";
            MessageBox(NULL, msg, title, MB_OK | MB_ICONWARNING);
        }
    }
    catch(const char *msg) { fatalAscii(msg); }
    catch(const std::wstring msg) { fatal(msg.c_str()); }
    catch(const std::string msg) { fatalAscii(msg.c_str()); }
    catch(const std::exception msg) { fatalAscii(msg.what()); }
    catch(...) { fatalAscii("An unknown error was detected while parsing command line parameters.\nThis is very wrong, program will now exit."); }
    if(fail) return -1;

	std::function<void(auint sleepms)> sleepFunc = [](auint ms) {
#if defined _WIN32
		Sleep(ms);
#else
#error This OS needs a sleep function.
#endif
    };

    const aubyte white[4] =  { 255, 255, 255, 255 };
	const aubyte green[4] =  {   0, 255,   0, 255 };
	const aubyte yellow[4] = {   0, 255, 255, 255 };
	const aubyte blue[4] =   { 255,   0,   0, 255 };
	const aubyte red[4] =    {   0,   0, 255, 255 };
	try {
		iconBitmaps->AddIcon(STATE_ICON_NORMAL, M8M_ICON_16X16_NORMAL);
		iconBitmaps->AddIcon(STATE_ICON_LISTENING, M8M_ICON_16X16_LISTENING);
		iconBitmaps->AddIcon(STATE_ICON_CLIENT_CONNECTED, M8M_ICON_16X16_CLIENT_CONNECTED);
		iconBitmaps->AddState(STATE_OK, white);
		iconBitmaps->AddState(STATE_INIT, green);
		iconBitmaps->AddState(STATE_WARN, yellow);
		iconBitmaps->AddState(STATE_ERROR, red);
		iconBitmaps->AddState(STATE_COOLDOWN, blue);
		iconBitmaps->SetIconHotspot(8, 7, 14 - 8, 13 - 7); // I just looked at the rasters. Don't ask!
	} catch(std::exception) {
		MessageBox(NULL, L"Error building the icons.", L"Fatal error!", MB_ICONERROR | MB_SYSTEMMODAL | MB_SETFOREGROUND);
		return 0;
	}
    const auto prgmInitialized = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
	bool run = true;
    bool nap = false;
    try {
	    while(run) {
		    iconBitmaps->SetCurrentIcon(STATE_ICON_NORMAL);
		    iconBitmaps->SetCurrentState(STATE_INIT);
            unique_ptr<Settings> configuration;
            commands::admin::RawConfig attempt;
            CFGLoadInfo load;
            {
                load.configFile = configFile;
                load.specified = configSpecified;
                try {
                    configuration.reset(LoadSettings(attempt, load));
                }
                catch(const char*) {} // no configuration --> checked later. Needs to be carefully considered anyway.
                catch(const std::wstring) {}
                catch(const std::string) {}
                catch(const std::exception) {}
            }
            if(nap) sleepFunc(500); // reinforce the idea I'm getting rebooted!
            std::unique_ptr<AbstractNotifyIcon> notify;
            if(invisible) notify.reset(new DummyNotifyIcon());
            else notify.reset(new NotifyIcon());
			std::vector<aubyte> ico;
			if(!configuration) {
				iconBitmaps->SetCurrentState(STATE_ERROR);
				notify->ShowMessage(L"ERROR: no configuration loaded.");
			}
			iconBitmaps->GetCompositedIcon(ico);
            notify->SetCaption(M8M_ICON_CAPTION);
			notify->SetIcon(ico.data(), M8M_ICON_SIZE, M8M_ICON_SIZE);
            // No matter if stuff is right or not, open the mini-servers so we can inspect state remotely.
		    Network network;
            MiniServers web(*notify, network); // hopefully those won't fail! Should I catch those? No way to be nice with this problem anyway!
            web.Init(*notify, *iconBitmaps);

            notify->AddMenuItem(L"Open app folder", []() { OpenFileExplorer(L""); });
		    notify->AddMenuSeparator();
		    web.admin.SetMessages(L"Enable web administration", L"Connect to web administration", L"Disable web admin");
		    notify->AddMenuSeparator();
		    web.monitor.SetMessages(L"Enable web monitor", L"Connect to web monitor", L"Disable web monitor");
		    notify->AddMenuSeparator();
		    notify->AddMenuSeparator();
		    notify->AddMenuItem(L"Exit ASAP", [&run]() { run = false; });
		    if(configuration && notify) notify->ShowMessage(L"Getting ready!\nLeave me some seconds to warm up..."); // otherwise we have errors already!
		    notify->BuildMenu();
		    notify->Tick();

            // Let's start with the serious stuff. First we need a place where we'll store sent shares waiting for the servers to signal accept/reject.
            std::map<ShareIdentifier, ShareFeedbackData> sentShares;
		    PoolManager remote(network);
            SyncMiningPerformanceWatcher performanceMetrics;
            std::unique_ptr<MinerSupport> importantMinerStructs;
            std::unique_ptr<NonceFindersInterface> miner;
            TrackedValues stats(remote);
            remote.connStateFunc = [&miner](const AbstractWorkSource &owner, PoolManager::ConnectionEvent ce) {
                using std::cout;
                cout<<"Pool ";
                if(owner.name.length()) cout<<'"'<<owner.name<<'"';
                else cout<<"0x"<<&owner;
                cout<<' ';
                switch(ce) {
                case PoolManager::ce_connecting: cout<<"connecting."; break;
                case PoolManager::ce_ready: cout<<"connected."; break;
                case PoolManager::ce_closing:
                case PoolManager::ce_failed:
                    cout<<(ce == PoolManager::ce_closing? "shutting down." : "DISCONNECTED!");
                    miner->SetWorkFactory(owner, std::unique_ptr<stratum::AbstractWorkFactory>());
                    break;
                case PoolManager::ce_failedResolve:
                    cout<<" !! Error connecting, could not resolve URL !!";
                    break;
                case PoolManager::ce_badSocket:
                    cout<<" !! Error connecting, could not create new socket !!";
                    break;
                case PoolManager::ce_failedConnect:
                    cout<<" !! Error connecting, could not initiate handshake !!";
                    break;
                case PoolManager::ce_noRoutes:
                    cout<<" !! Error connecting, no routes !!";
                    break;
                default: std::cout<<"??Impossible! Code out of sync??";
                }
                cout<<std::endl;
            };
		    remote.dispatchFunc = [&miner](const AbstractWorkSource &pool, std::unique_ptr<stratum::AbstractWorkFactory> &newWork) {
			    if(miner) miner->SetWorkFactory(pool, newWork);
                // It is fine for the miner to not be there. Still connect so the pool can signal me as non-working.
		    };
            remote.diffChangeFunc = [&miner](const AbstractWorkSource &pool, stratum::WorkDiff &diff) {
			    if(miner) miner->SetDifficulty(pool, diff);
		    };
            remote.stratErrFunc = [](const AbstractWorkSource &owner, asizei i, int errorCode, const std::string &message) {
                using std::cout;
                cout<<"Pool ";
                if(owner.name.length()) cout<<'"'<<owner.name<<'"';
                else cout<<"0x"<<&owner;
                cout<<" reported stratum error ["<<std::to_string(i)<<']'<<std::endl;
                cout<<"    error code "<<std::dec<<errorCode<<"=0x"<<std::hex<<errorCode<<std::dec<<std::endl;
                cout<<"    \""<<message<<"\""<<std::endl;
		    };
            remote.onPoolCommand = [&stats](const AbstractWorkSource &pool) {
                for(auto &update : stats.poolShares) {
                    if(update.src == &pool) {
                        update.lastActivity = std::chrono::system_clock::now();
                        break;
                    }
                }
            };

            if(configuration) {
                remote.SetReconnectDelay(configuration->reconnDelay);
                remote.AddPools(configuration->pools);
                stats.poolShares.resize(configuration->pools.size());
                for(const auto &info : ProcessingNodesFactory::GetAlgoInformations()) remote.InitPools(info);
                
                for(asizei index = 0; index < remote.GetNumServers(); index++) {
                    auto &pool(remote.GetServer(index));
                    stats.poolShares[index].src = &pool;
                    pool.shareResponseCallback = [index, &sentShares, &stats](const AbstractWorkSource &me, asizei shareID, StratumShareResponse stat) {
                        ShareIdentifier key { &me, index, shareID };
                        auto match(sentShares.find(key));
                        if(match != sentShares.cend()) {
                            ShareFeedback(key, match->second, stat);
                            for(auto &entry : stats.poolShares) {
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
                            cout<<"Pool ["<<index<<"] ";
                            if(me.name.length()) cout<<'"'<<me.name<<"\" ";
                            cout<<"signaled untracked share "<<shareID<<std::endl;
                            // Maybe I should be throwing there.
                        }
                    };
                    pool.workerAuthCallback = [&configuration, &remote, &notify, &iconBitmaps, &ico](const AbstractWorkSource &owner, const std::string &worker, StratumState::AuthStatus status) {
                        if(status == StratumState::as_failed) {
                            notify->ShowMessage(L"At least one worker failed to authenticate!");
			                iconBitmaps->SetCurrentState(STATE_ERROR);
			                iconBitmaps->GetCompositedIcon(ico);
			                notify->SetIcon(ico.data(), M8M_ICON_SIZE, M8M_ICON_SIZE);
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
		            };
                }

                if(remote.Activate(configuration->algo) == 0) {
                    notify->ShowMessage(L"No remote servers.");
			        if(iconBitmaps) {
                        iconBitmaps->SetCurrentState(STATE_ERROR);
			            iconBitmaps->GetCompositedIcon(ico);
                    }
			        notify->SetIcon(ico.data(), M8M_ICON_SIZE, M8M_ICON_SIZE);
                }
            }
            stats.prgStart = prgmInitialized.count();

            OpenCL12Wrapper api;
            commands::monitor::ConfigInfoCMD::ConfigDesc configInfoCMDReply;
            asizei numDevices = 0;
            for(auto &p : api.platforms) {
                for(auto &d : p.devices) numDevices++;
            }
            performanceMetrics.SetNumDevices(numDevices);
            stats.performance = &performanceMetrics;
            stats.deviceShares.resize(numDevices);
            if(configuration) {
                rapidjson::Value::ConstMemberIterator selecting = configuration->implParams.FindMember(configuration->algo.c_str());
                if(selecting == configuration->implParams.MemberEnd()) throw "No settings found for algorithm \"" + configuration->algo + '"';
                const rapidjson::Value &container(selecting->value);
                selecting = container.FindMember(configuration->impl.c_str());
                const rapidjson::Value nullValue(rapidjson::kNullType);
                const rapidjson::Value *implParams = selecting == container.MemberEnd()? &nullValue : &selecting->value;

                ProcessingNodesFactory helper(sleepFunc);
                helper.NewDriver(configuration->driver.c_str(), configuration->algo.c_str(), configuration->impl.c_str());
                for(asizei i = 0; i < remote.GetNumServers(); i++) {
                    bool added = helper.AddPool(remote.GetServer(i));
                    stats.poolShares[i].active = added;
                }
                if(implParams->IsNull() == false) helper.ExtractSelectedConfigurations(*implParams);
                importantMinerStructs = std::move(helper.SelectSettings(api, ErrorsToSTDOUT));
                for(auto &build : importantMinerStructs->niceDevices) helper.BuildAlgos(importantMinerStructs->algo, build);
                miner = helper.Finished("kernels/", [&performanceMetrics](asizei gpuindex, bool found, std::chrono::microseconds elapsed) {
                    performanceMetrics.Completed(gpuindex, found, elapsed);
                }); // The miner really started a bit before this returns... anyway
                helper.DescribeConfigs(configInfoCMDReply, numDevices, importantMinerStructs->algo);
		        stats.minerStart = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            }
            
            TrackedAdminValues admin(load, attempt);
            
            WebCommands parsers;
            if(importantMinerStructs) {
                AlgoIdentifier algoID;
                aulong versionHash = 0;
                if(importantMinerStructs->algo.size()) {
                    algoID = importantMinerStructs->algo[0]->identifier;
                    versionHash = importantMinerStructs->algo[0]->GetVersioningHash();
                }
                std::vector<auint> devConfMap(numDevices);
                for(auto &el : devConfMap) el = auint(-1);
                numDevices = 0;
                for(const auto &p : api.platforms) {
                    for(const auto &d : p.devices) {
                        for(const auto &check : importantMinerStructs->niceDevices) {
                            for(const auto &test : check.devices) {
                                if(test.clid == d.clid) devConfMap[numDevices] = auint(test.configIndex);
                            }
                        }
                        numDevices++;
                    }
                }
                RegisterMonitorCommands(parsers, web.monitor, api, remote, stats, std::make_pair(algoID, versionHash), devConfMap, importantMinerStructs->devConfReasons, configInfoCMDReply);
            }
            else {
                std::vector<auint> devConfMap(numDevices);
                for(auto &el : devConfMap) el = auint(-1);
                std::vector< std::vector<MinerSupport::ConfReasons> > devConfReasons; //!< \todo: perhaps this could be "no config loaded", but it's not a per-device reject reason.
                RegisterMonitorCommands(parsers, web.monitor, api, remote, stats, std::make_pair(AlgoIdentifier(), 0), devConfMap, devConfReasons, configInfoCMDReply);
		        RegisterMonitorCommands(parsers, web.admin, api, remote, stats, std::make_pair(AlgoIdentifier(), 0), devConfMap, devConfReasons, configInfoCMDReply);
            }
		    RegisterAdminCommands(parsers, web.admin, admin);

            MinerMessagePump everything(*notify, *iconBitmaps, network, remote, stats);
            everything.miner = miner.get();
            run = everything.Pump(sleepFunc, run, web, stats.firstNonce, sentShares, admin);
            nap = true;
	    }
    }
    catch(const char *msg) { fatalAscii(msg); }
    catch(const std::wstring msg) { fatal(msg.c_str()); }
    catch(const std::string msg) { fatalAscii(msg.c_str()); }
    catch(const std::exception msg) { fatalAscii(msg.what()); }
	return 0;
}
