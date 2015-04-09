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
#include "Connections.h"
#include "ProcessingNodesFactory.h"
#include "AlgoMiner.h"
#include <iomanip>
#include "M8MIcon.h"
#include "../Common/AREN/SharedUtils/AutoConsole.h"
#include "../Common/AREN/SharedUtils/AutoCommandLine.h"
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
    sharedUtils::system::AutoGoDir<true> cfgDir(loadInfo.configDir.c_str(), true);
    if(!cfgDir.Changed()) throw std::wstring(L"Could not go to configuration directory \"") + loadInfo.configDir + L'"';

	using namespace rapidjson;
	CFGLoadErrInfo failure;
    loadInfo.specified = true;
    if(loadInfo.configFile.empty()) {
        loadInfo.specified = false;
        loadInfo.configFile = L"init.json";
    }
	const std::wstring ori(loadInfo.configFile);
	asizei attempt = loadInfo.specified? 1 : 0;
	while(attempt < 2 && LoadConfigJSON(loadAttempt.good, failure, loadInfo.configFile)) {
		if(loadAttempt.good.IsObject() == false) {
			loadAttempt.good.SetNull();
			loadAttempt.errCode = failure.errCode;
			loadAttempt.errOff = failure.errOff;
			loadAttempt.errDesc = std::move(failure.errDesc);
			loadAttempt.raw = failure.raw.get();
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
    NotifyIcon &notify;
    IconCompositer<16, 16> &iconBitmaps;
    Network &network;
    Connections &remote;
    TrackedValues &stats;

    NonceFindersInterface *miner = nullptr;

    MinerMessagePump(NotifyIcon &icon, IconCompositer<16, 16> &rasters, Network &net, Connections &servers, TrackedValues &track)
        : notify(icon), iconBitmaps(rasters), network(net), remote(servers), stats(track) { }

    bool Pump(const std::function<void(auint ms)> &sleepFunc, bool &run, MiniServers &web, aulong &firstNonce, std::map<ShareIdentifier, ShareFeedbackData> &sentShares, TrackedAdminValues &admin) {
		bool firstShare = true;
		asizei sinceActivity = 0;
		std::vector<Network::SocketInterface*> toRead, toWrite;
        bool deadServersSignaled = false;
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
			if(!updated) {
				std::vector<Network::SocketInterface*> dummy;
				web.monitor.Refresh(dummy, dummy); // this will allow the server to shut down if necessary
				web.admin.Refresh(dummy, dummy);
				if(toRead.size()) sinceActivity += POLL_PERIOD_MS;
				if(sinceActivity >= TIMEOUT_MS && deadServersSignaled == false) {
                    notify.ShowMessage(L"No activity from servers in 120 seconds.");
			        std::array<aubyte, M8M_ICON_SIZE * M8M_ICON_SIZE * 4> ico;
			        iconBitmaps.SetCurrentState(STATE_ERROR);
			        iconBitmaps.GetCompositedIcon(ico);
			        notify.SetIcon(ico.data(), M8M_ICON_SIZE, M8M_ICON_SIZE);
                    
					throw std::exception("No activity from connections in 120 seconds. Fatal network fail? I give up");
                    deadServersSignaled = true;
				}
			}
			else {
				sinceActivity = 0;
				remote.Refresh(toRead, toWrite);
				web.monitor.Refresh(toRead, toWrite);
				web.admin.Refresh(toRead, toWrite);
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

	    std::array<aubyte, M8M_ICON_SIZE * M8M_ICON_SIZE * 4> ico;
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

    StartParams cmdParams;

	/* M8M is super spiffy and so minimalistic I often forgot it's already running.
	Running multiple instances might make sense in the future (for example to mine different algos on different cards)
	but it's not supported for the time being. Having multiple M8M instances doing the same thing will only cause driver work and GPU I$ to work extra hard. */
    OSUniqueChecker onlyOne;
    if(cmdParams.secondaryInstance == false) {
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

    std::unique_ptr<sharedUtils::system::AutoConsole<false>> handyOutputForDebugging;
#if defined(_WIN32) && (defined(_DEBUG) || defined(RELEASE_WITH_CONSOLE))
    cmdParams.allocConsole = true;
#endif
    if(cmdParams.allocConsole) {
	    handyOutputForDebugging = std::make_unique<sharedUtils::system::AutoConsole<false>>();
	    handyOutputForDebugging->Enable();
    }
	

    std::wstring dataDirBase;
	#if defined(_WIN32)
		PWSTR lappData = nullptr;
		HRESULT got = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &lappData);
		if(got != S_OK) {
			MessageBox(NULL, L"Could not locate %LOCALAPPDATA% directory.\nThis is a fatal error.", L"Directory error!", MB_ICONERROR | MB_SYSTEMMODAL | MB_SETFOREGROUND);
			return 0;
		}
		ScopedFuncCall clear([lappData]() { CoTaskMemFree(lappData); });
		dataDirBase = lappData;
		dataDirBase += DIR_SEPARATOR  L"M8M" DIR_SEPARATOR;
	#else
	#error change to some working/settings directory for some OS
	#endif

    const bool confDirRedirected = cmdParams.cfgDir.empty() == false;
	if(cmdParams.cfgDir.empty()) {
        cmdParams.cfgDir = dataDirBase + L"conf";
	}

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
	IconCompositer<16, 16> iconBitmaps;
	try {
		iconBitmaps.AddIcon(STATE_ICON_NORMAL, M8M_ICON_16X16_NORMAL);
		iconBitmaps.AddIcon(STATE_ICON_LISTENING, M8M_ICON_16X16_LISTENING);
		iconBitmaps.AddIcon(STATE_ICON_CLIENT_CONNECTED, M8M_ICON_16X16_CLIENT_CONNECTED);
		iconBitmaps.AddState(STATE_OK, white);
		iconBitmaps.AddState(STATE_INIT, green);
		iconBitmaps.AddState(STATE_WARN, yellow);
		iconBitmaps.AddState(STATE_ERROR, red);
		iconBitmaps.AddState(STATE_COOLDOWN, blue);
		iconBitmaps.SetIconHotspot(8, 7, 14 - 8, 13 - 7); // I just looked at the rasters. Don't ask!
	} catch(std::exception) {
		MessageBox(NULL, L"Error building the icons.", L"Fatal error!", MB_ICONERROR | MB_SYSTEMMODAL | MB_SETFOREGROUND);
		return 0;
	}

    auto fatal = [](const wchar_t *msg) {
		MessageBox(NULL, msg, L"Fatal error!", MB_ICONERROR | MB_SYSTEMMODAL | MB_SETFOREGROUND);
		return 0;
    };
    auto fatalAscii = [&fatal](const char *msg) {
        std::vector<wchar_t> unicode(strlen(msg) + 1); // not converted correctly, I could/should use rapidjson here but is it worth?
        for(asizei cp = 0; msg[cp]; cp++) unicode[cp] = msg[cp];
        unicode[strlen(msg)] = 0;
        fatal(unicode.data());
    };
    const auto prgmInitialized = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
	bool run = true;
    bool nap = false;
    try {
	    while(run) {
		    iconBitmaps.SetCurrentIcon(STATE_ICON_NORMAL);
		    iconBitmaps.SetCurrentState(STATE_INIT);
            unique_ptr<Settings> configuration;
            commands::admin::RawConfig attempt;
            CFGLoadInfo load;
            {
                load.configDir = cmdParams.cfgDir;
                load.configFile = cmdParams.cfgFile;
                try {
                    configuration.reset(LoadSettings(attempt, load));
                }
                catch(const char*) {} // no configuration --> checked later. Needs to be carefully considered anyway.
                catch(const std::wstring) {}
                catch(const std::string) {}
                catch(const std::exception) {}
            }
            if(nap) sleepFunc(500); // reinforce the idea I'm getting rebooted!
            NotifyIcon notify;
		    {
			    std::array<aubyte, M8M_ICON_SIZE * M8M_ICON_SIZE * 4> ico;
			    if(!configuration) {
				    iconBitmaps.SetCurrentState(STATE_ERROR);
				    notify.ShowMessage(L"ERROR: no configuration loaded.");
			    }
			    iconBitmaps.GetCompositedIcon(ico);
			    notify.SetCaption(M8M_ICON_CAPTION);
			    notify.SetIcon(ico.data(), 16, 16);
		    }
            // No matter if stuff is right or not, open the mini-servers so we can inspect state remotely.
		    Network network;
            MiniServers web(notify, network); // hopefully those won't fail! Should I catch those? No way to be nice with this problem anyway!
            web.Init(notify, iconBitmaps);
        
		    notify.AddMenuItem(L"Open user app folder", [&dataDirBase]() { OpenFileExplorer(dataDirBase.c_str()); });
		    notify.AddMenuSeparator();
		    notify.ShowMessage(L"Getting ready!\nLeave me some seconds to warm up...");
		    web.admin.SetMessages(L"Enable web administration", L"Connect to web administration", L"Disable web admin");
		    notify.AddMenuSeparator();
		    web.monitor.SetMessages(L"Enable web monitor", L"Connect to web monitor", L"Disable web monitor");
		    notify.AddMenuSeparator();
		    notify.AddMenuSeparator();
		    notify.AddMenuItem(L"Exit ASAP", [&run]() { run = false; });
		    notify.BuildMenu();
		    notify.Tick();

            // Let's start with the serious stuff. First we need a place where we'll store sent shares waiting for the servers to signal accept/reject.
            std::map<ShareIdentifier, ShareFeedbackData> sentShares;
		    Connections remote(network);
            SyncMiningPerformanceWatcher performanceMetrics;
            std::unique_ptr<MinerSupport> importantMinerStructs;
            std::unique_ptr<NonceFindersInterface> miner;
            if(configuration) {
                for(const auto &pool : configuration->pools) remote.AddPool(*pool);
            }
            TrackedValues stats(remote, prgmInitialized.count());
		    if(remote.GetNumPoolsAdded() == 0) {
			    if(configuration) { // otherwise, keep the bad config message
                    notify.ShowMessage(L"No remote servers.");
			        std::array<aubyte, M8M_ICON_SIZE * M8M_ICON_SIZE * 4> ico;
			        iconBitmaps.SetCurrentState(STATE_ERROR);
			        iconBitmaps.GetCompositedIcon(ico);
			        notify.SetIcon(ico.data(), M8M_ICON_SIZE, M8M_ICON_SIZE);
                }
		    }
            remote.SetNoAuthorizedWorkerCallback([&configuration, &remote, &notify, &iconBitmaps]() {
                if(configuration && remote.GetNumPoolsAdded()) {
                    notify.ShowMessage(L"No workers logged to server!");
			        std::array<aubyte, M8M_ICON_SIZE * M8M_ICON_SIZE * 4> ico;
			        iconBitmaps.SetCurrentState(STATE_ERROR);
			        iconBitmaps.GetCompositedIcon(ico);
			        notify.SetIcon(ico.data(), M8M_ICON_SIZE, M8M_ICON_SIZE);
                }
		    });
		    remote.dispatchFunc = [&miner](AbstractWorkSource &pool, stratum::AbstractWorkUnit *wu) {
			    std::unique_ptr<stratum::AbstractWorkUnit> own(wu);
                NonceOriginIdentifier who(&pool, own->job);
			    if(miner) miner->RefreshBlockData(who, own);
                //! It is fine for the miner to not be there. Still connect so the pool can signal me as non-working.
		    };
            for(asizei index = 0; index < remote.GetNumServers(); index++) {
                remote.GetServer(index).shareResponseCallback = [index, &sentShares, &stats](const AbstractWorkSource &me, asizei shareID, StratumShareResponse stat) {
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
                                entry.acceptedDiff += match->second.shareDiff;
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
                        cout<<"Pool ["<<index<<"] ";
                        if(me.name.length()) cout<<'"'<<me.name<<"\" ";
                        cout<<"signaled untracked share "<<shareID<<endl;
                        // Maybe I should be throwing there.
                    }
                };
            }

            OpenCL12Wrapper api;
            commands::monitor::ConfigInfoCMD::ConfigDesc configInfoCMDReply;
            asizei numDevices = 0;
            for(auto &p : api.platforms) {
                for(auto &d : p.devices) numDevices++;
            }
            performanceMetrics.SetNumDevices(numDevices);
            stats.performance = &performanceMetrics;
            stats.deviceShares.resize(numDevices);
            {
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
            }
		    stats.minerStart = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            
            TrackedAdminValues admin;
            admin.customConfDir = confDirRedirected;
            
            WebCommands parsers;
            {
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
		        RegisterMonitorCommands(parsers, web.admin, api, remote, stats, std::make_pair(algoID, versionHash), devConfMap, importantMinerStructs->devConfReasons, configInfoCMDReply);
		        RegisterAdminCommands(parsers, web.admin, admin);
            }
            remote.onPoolCommand = [&stats](const AbstractWorkSource &pool) {
                for(auto &update : stats.poolShares) {
                    if(update.src == &pool) {
                        update.lastActivity = std::chrono::system_clock::now();
                        break;
                    }
                }
            };

            MinerMessagePump everything(notify, iconBitmaps, network, remote, stats);
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
