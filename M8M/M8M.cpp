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
#include "AbstractThreadedMiner.h"
#include "AlgoFactories/QubitCL12.h"
#include "AlgoFactories/GroestlMYRCL12.h"
#include <iomanip>
#include "../Common/NotifyIcon.h"
#include "M8MIcon.h"
#include "WebMonitorTracker.h"
#include "../Common/AREN/SharedUtils/AutoConsole.h"
#include "../Common/AREN/SharedUtils/AutoCommandLine.h"

#include "commands/Monitor/SystemInfoCMD.h"
#include "commands/Monitor/AlgoCMD.h"
#include "commands/Monitor/PoolCMD.h"
#include "commands/Monitor/DeviceConfigCMD.h"
#include "commands/Monitor/RejectReasonCMD.h"
#include "commands/Monitor/ConfigInfoCMD.h"
#include "commands/Monitor/ScanTime.h"
#include "commands/Monitor/DeviceShares.h"
#include "commands/Monitor/PoolShares.h"
#include "commands/Monitor/UptimeCMD.h"
#include "commands/ExtensionListCMD.h"
#include "commands/UnsubscribeCMD.h"
#include "commands/UpgradeCMD.h"
#include "commands/VersionCMD.h"

#include "TimedValueStream.h"

#include "../Common/AREN/SharedUtils/OSUniqueChecker.h"

#if defined _WIN32
#include <ShlObj.h>
#include <KnownFolders.h>
#endif

#include "../Common/AREN/SharedUtils/dirControl.h"

#include "IconCompositer.h"

Connections* InstanceConnections(const Settings *settings, Network &manager, Connections::DispatchCallback df) {
	std::unique_ptr<Connections> ret(new Connections(manager, df));
	if(settings) {
		for(asizei loop = 0; loop < settings->pools.size(); loop++) ret->AddPool(*settings->pools[loop]);
	}
	return ret.release();
}


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

struct TrackedValues : commands::monitor::ScanTime::DeviceTimeProviderInterface, commands::monitor::DeviceShares::ValueSourceInterface, commands::monitor::PoolShares::ShareProviderInterface,
                       commands::monitor::UptimeCMD::StartTimeProvider {
	std::vector<TimedValueStream> shareTime;
	std::vector<commands::monitor::DeviceShares::ShareStats> shares;
	const Connections &servers;

    aulong prgStart;
    aulong minerStart;
    aulong firstNonce;

	TrackedValues(const Connections &src, aulong progStart) : servers(src), prgStart(progStart), minerStart(0), firstNonce(0) { }

	void Resize(asizei numDevices) {
		shareTime.resize(numDevices);
		shares.resize(numDevices);
	}

	// commands::monitor::ScanTime::DeviceTimeProviderInterface
	bool GetSTMinMax(std::chrono::microseconds &min, std::chrono::microseconds &max, asizei devIndex) const {
		if(devIndex < shareTime.size()) {
			min = shareTime[devIndex].GetMin();
			max = shareTime[devIndex].GetMax();
		}
		return devIndex < shareTime.size();
	}

	bool GetSTSlidingAvg(std::chrono::microseconds &window, asizei devIndex) const {
		if(devIndex < shareTime.size()) window = shareTime[devIndex].GetAverage();
		return devIndex < shareTime.size();
	}
	
	bool GetSTLast(std::chrono::microseconds &last, std::chrono::microseconds &avg, asizei devIndex) const {
		if(devIndex < shareTime.size()) shareTime[devIndex].GetLast(last, avg);
		return devIndex < shareTime.size();
	}

	void GetSTWindow(std::chrono::minutes &sliding) const {
		sliding = shareTime[0].GetWindow();
	}
	
	bool GetDSS(commands::monitor::DeviceShares::ShareStats &out, asizei dl) {
		if(dl < shares.size()) {
			out.bad = shares[dl].bad;
			out.good = shares[dl].good;
			out.stale = shares[dl].stale;
			out.lastResult = shares[dl].lastResult;
		}
		return dl < shares.size();
	}

	
	asizei GetNumPools() const { return servers.GetNumServers(); }
	std::string GetPoolName(asizei pool) const { return std::string(servers.GetServer(pool).name); }
	asizei GetNumWorkers(asizei pool) const {  return servers.GetServer(pool).GetNumUsers(); }
	StratumState::WorkerNonceStats GetWorkerStats(asizei pool, asizei worker) { return servers.GetServer(pool).GetUserShareStats(worker); }

    aulong GetStartTime(commands::monitor::UptimeCMD::StartTime st) {
        using namespace commands::monitor;
        switch(st) {
            case UptimeCMD::st_program: return prgStart;
            case UptimeCMD::st_hashing: return minerStart;
            case UptimeCMD::st_firstNonce: return firstNonce;
        }
        throw std::exception("GetStartTime, impossible, code out of sync?");
    }
};


#include "commands/Admin/ConfigFileCMD.h"
#include "commands/Admin/SaveRawConfigCMD.h"
#include "commands/Admin/ReloadCMD.h"
#include "commands/Admin/GetRawConfigCMD.h"


struct TrackedAdminValues : public commands::admin::ConfigFileCMD::ConfigInfoProviderInterface {
	std::wstring configFile, configDir;
	bool specified, redirected, valid;

	commands::admin::GetRawConfigCMD::RawConfig loadedConfig;

	asizei reloadRequested;
	bool willReloadListening;

	TrackedAdminValues() : specified(false), redirected(false), valid(false), reloadRequested(0), willReloadListening(false) { }

	std::wstring Filename() const { return configFile; }
	bool Explicit() const { return specified; }
	bool Redirected() const { return redirected; }
	bool Valid() const { return valid; }
};


typedef std::vector< std::unique_ptr<commands::AbstractCommand> > WebCommands;


template<typename CreateClass, typename Param>
void SimpleCommand(WebCommands &persist, WebTrackerOnOffConn &mon, Param &param) {
	std::unique_ptr<CreateClass> build(new CreateClass(param));
	mon.RegisterCommand(*build);
	persist.push_back(std::move(build));
}

template<typename MiningProcessorsProvider, typename WebsocketService>
void RegisterMonitorCommands(WebCommands &persist, WebsocketService &mon, AbstractMiner<MiningProcessorsProvider> &miner, const std::unique_ptr<Connections> &connections, TrackedValues &tracking) {
	using namespace commands::monitor;
	MiningProcessorsProvider &procs(miner.GetProcessersProvider());
	SimpleCommand< SystemInfoCMD<MiningProcessorsProvider> >(persist, mon, procs);
	SimpleCommand<AlgoCMD>(persist, mon, miner);
	{
		auto getPoolURL = [&connections](const AbstractWorkSource &pool) -> std::string {
			// if(connections.get() == nullptr) throw std::exception("Impossible. This was guaranteed to not happen by program structure.");
			// If pool is not set then this cannot be called. But if this is callable, pool is set, therefore connections are there.
			const Network::ConnectedSocketInterface &socket(connections->GetConnection(pool));
			return socket.PeerHost() + ':' + socket.PeerPort();
		};
		std::unique_ptr<PoolCMD> build(new PoolCMD(miner, getPoolURL));
		mon.RegisterCommand(*build);
		persist.push_back(std::move(build));
	}
	SimpleCommand<DeviceConfigCMD>(persist, mon, miner);
	SimpleCommand<RejectReasonCMD>(persist, mon, miner);
	SimpleCommand<ConfigInfoCMD>(persist, mon, miner);
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


void RegisterAdminCommands(WebCommands &persist, WebAdminTracker &mon, TrackedAdminValues &tracking) {
	using namespace commands::admin;
	SimpleCommand<ConfigFileCMD>(persist, mon, tracking);
	SimpleCommand<GetRawConfigCMD>(persist, mon, tracking.loadedConfig);
	{
		std::unique_ptr<SaveRawConfigCMD> build(new SaveRawConfigCMD(tracking.configDir.c_str(), tracking.configFile.c_str()));
		mon.RegisterCommand(*build);
		persist.push_back(std::move(build));
	}
	SimpleCommand<ReloadCMD>(persist, mon, [&tracking]() {
		tracking.reloadRequested = 1;
		return tracking.willReloadListening;
	});
}


template<typename Service, typename Values>
struct WebPair {
	Service &service;
	Values &values;
	WebPair(Service &s, Values &v) : service(s), values(v) { }
};


struct WebServices {
	WebPair<WebMonitorTracker, TrackedValues> monitor;
	WebPair<WebAdminTracker, TrackedAdminValues> admin;
	WebServices(WebMonitorTracker &mon, TrackedValues &monvals, WebAdminTracker &adm, TrackedAdminValues &admvals)
		: monitor(mon, monvals), admin(adm, admvals) { }
};



MinerInterface* InstanceProcessingNodes(WebCommands &persist, const Settings *settings, WebServices &track, const std::unique_ptr<Connections> &connections, std::function<void(auint ms)> sleepFunc) {
	// invalid settings --> init OpenCL. It's a bit sub-optimal but easier thing to do to keep the whole thing more or less flowing.
	if(!settings || !_stricmp("opencl", settings->driver.c_str()) || !_stricmp("ocl", settings->driver.c_str())) {
		std::vector< std::unique_ptr< AlgoFamily<OpenCL12Wrapper> > > algos;
		std::unique_ptr< AlgoFamily<OpenCL12Wrapper> > add(new QubitCL12(true, ErrorsToSTDOUT));
		algos.push_back(std::move(add));
		add.reset(new GroestlMYRCL12(true, ErrorsToSTDOUT));
		algos.push_back(std::move(add));
		std::unique_ptr< AbstractThreadedMiner<OpenCL12Wrapper> > ret(new AbstractThreadedMiner<OpenCL12Wrapper>(algos, sleepFunc));
		RegisterMonitorCommands(persist, track.monitor.service, *ret, connections, track.monitor.values);
		RegisterMonitorCommands(persist, track.admin.service, *ret, connections, track.monitor.values);
		RegisterAdminCommands(persist, track.admin.service, track.admin.values);
		if(settings) ret->CheckNonces(settings->checkNonces);
		return ret.release();
	}
	throw std::exception("Driver string invalid.");
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

#define PATH_TO_WEBAPPS L".." DIR_SEPARATOR L"web" DIR_SEPARATOR

#define WEBMONITOR_PATH PATH_TO_WEBAPPS L"monitor_localhost.html"
#define WEBADMIN_PATH PATH_TO_WEBAPPS L"admin_localhost.html"



//! Not very efficient to scan multiple times but it gives me easy interface. Double pointers + const = ouch, but easy
bool ParseParam(std::wstring &value, const asizei argc, wchar_t **argv, const wchar_t *name, const wchar_t *defaultValue) {
	bool found = false;
	for(asizei loop = 0; loop < argc; loop++) {
		if(wcsncmp(argv[loop], L"--", 2) == 0 && wcscmp(argv[loop] + 2, name)) {
			found = true;
			value.clear();
			if(loop + 1 < argc) {
				loop++;
				asizei limit;
				for(limit = loop; limit < argc; limit++) {
					if(wcsncmp(argv[loop], L"--", 2) == 0) break;
				}
				for(; loop < limit; loop++) {
					if(value.length()) value += L" ";
					value += argv[loop];
				}
			}
			break;
		}
	}
	if(!found) value = defaultValue;
	return found;
}


/*! Helper struct to keep LoadConfigJSON easier to describe. */
struct CFGLoadErrInfo {
	std::unique_ptr<char[]> raw;
	std::string errDesc;
	asizei errCode;
	asizei errOff;
};

#define CFG_FILE_MAX_BYTES_ON_ERROR (1024 * 1024)


#include <rapidjson/filereadstream.h>
#include <rapidjson/encodedstream.h>
#include <rapidjson/error/en.h>

/*! A configuration file is a JSON object. This function will try to open the specified filename and load a JSON object from it.
If file cannot be read, it will return false. Even if true is returned, the returned cfg is valid only if an object.
This might be not the case if parsing failed. In this case, errInfo will be properly populated. */
bool LoadConfigJSON(rapidjson::Document &root, CFGLoadErrInfo &errInfo, const std::wstring &filename) {
	using std::unique_ptr;
	using namespace rapidjson;
	FILE *jsonFile = nullptr;
	if(_wfopen_s(&jsonFile, filename.c_str(), L"rb")) return false;
	ScopedFuncCall autoClose([jsonFile]() { fclose(jsonFile); });

	char jsonReadBuffer[512];
	FileReadStream jsonIN(jsonFile, jsonReadBuffer, sizeof(jsonReadBuffer));
	AutoUTFInputStream<unsigned __int32, FileReadStream> input(jsonIN);
	root.ParseStream< 0, AutoUTF<unsigned> >(input);
	if(root.HasParseError()) {
		errInfo.errOff = root.GetErrorOffset();
		errInfo.errCode = root.GetParseError();
		errInfo.errDesc = GetParseError_En(root.GetParseError());
		_fseeki64(jsonFile, 0, SEEK_END);
		aulong fileSize = _ftelli64(jsonFile);
		fileSize = min(fileSize, CFG_FILE_MAX_BYTES_ON_ERROR);
		errInfo.raw.reset(new char[asizei(fileSize) + 1]);
		errInfo.raw[asizei(fileSize)] = 0;
		rewind(jsonFile);
		if(fread(errInfo.raw.get(), asizei(fileSize), 1, jsonFile) < 1) throw std::wstring(L"Fatal read error while trying to load \"") + filename + L"\".";
	}
	return true;
}


Settings* BuildSettings(std::vector<std::string> &errors, const rapidjson::Value &root) {
	unique_ptr<Settings> ret(new Settings);
	if(root.IsObject() == false) {
		errors.push_back("Valid configurations must be objects.");
		return nullptr;
	}
	using namespace rapidjson;
	const Value::ConstMemberIterator pools = root.FindMember("pools");
	auto mkString = [](const Value &jv) { return std::string(jv.GetString(), jv.GetStringLength()); };
	if(pools == root.MemberEnd()) errors.push_back("No pools specified in config file."); // but keep going, not fatal.
	else if(pools->value.IsObject() == false) errors.push_back("Pool list must be an object.");
	else {
		for(Value::ConstMemberIterator keys = pools->value.MemberBegin(); keys != pools->value.MemberEnd(); ++keys) {
			const Value &load(keys->value);
			if(!load.IsObject()) {
				errors.push_back(std::string("pools[") + mkString(keys->name) + "] is not an object. Ignored.");
				continue;
			}
			std::string fieldList;
			bool valid = true;
			auto reqString = [&fieldList, &valid, &load](const char *key) -> Value::ConstMemberIterator {
				Value::ConstMemberIterator field = load.FindMember(key);
				bool good = field != load.MemberEnd() && field->value.IsString();
				if(!good) {
					if(fieldList.length()) fieldList += ", ";
					fieldList += key;
				}
				valid &= good;
				return field;
			};
			const auto addr = reqString("url");
			const auto user = reqString("user");
			const auto psw = reqString("pass");
			const auto algo = reqString("algo");
			if(!valid) {
				errors.push_back(std::string("pools[") + mkString(keys->name) + "] ignored, invalid fields: " + fieldList);
				continue;
			}
			unique_ptr<PoolInfo> add(new PoolInfo(mkString(keys->name), mkString(addr->value), mkString(user->value), mkString(psw->value)));
			add->algo = mkString(algo->value);
			const auto proto(load.FindMember("protocol"));
			const auto coinDiff(load.FindMember("coinDiffMul"));
			const auto merkleMode(load.FindMember("merkleMode"));
			if(proto != load.MemberEnd() && proto->value.IsString()) add->appLevelProtocol = mkString(proto->value);
			if(coinDiff != load.MemberEnd()) {
				if(coinDiff->value.IsUint()) add->diffOneMul = coinDiff->value.GetUint();
				else if(coinDiff->value.IsUint64()) add->diffOneMul = coinDiff->value.GetUint64();
			}
			if(merkleMode != load.MemberEnd() && merkleMode->value.IsString()) {
				std::string mmode(mkString(merkleMode->value));
				if(mmode == "SHA256D") add->merkleMode = PoolInfo::mm_SHA256D;
				else if(mmode == "singleSHA256") add->merkleMode = PoolInfo::mm_singleSHA256;
				else throw std::string("Unknown merkle mode: \"" + mmode + "\".");
			}
			ret->pools.push_back(std::move(add));
		}
		if(!ret->pools.size()) errors.push_back(std::string("no valid pool configurations!"));
	}
	{
		Value::ConstMemberIterator driver = root.FindMember("driver");
		Value::ConstMemberIterator algo = root.FindMember("algo");
		Value::ConstMemberIterator impl = root.FindMember("impl");
		if(driver != root.MemberEnd() && driver->value.IsString()) ret->driver = mkString(driver->value);
		if(algo != root.MemberEnd() && algo->value.IsString()) ret->algo = mkString(algo->value);
		if(impl != root.MemberEnd() && impl->value.IsString()) ret->impl = mkString(impl->value);
		Value::ConstMemberIterator checkNonces = root.FindMember("checkNonces");
		if(checkNonces != root.MemberEnd() && checkNonces->value.IsBool()) ret->checkNonces = checkNonces->value.GetBool();
	}
	Value::ConstMemberIterator implParams = root.FindMember("implParams");
	if(implParams != root.MemberEnd()) ret->implParams.CopyFrom(implParams->value, ret->implParams.GetAllocator());
	return ret.release();
}


//! \return false if time to exit, otherwise call that again as a full reboot was requested
bool ProgramBody(std::function<void(auint sleepms)> sleepFunc, IconCompositer<16, 16> &iconBitmaps, const std::wstring &dataDirBase, const std::chrono::seconds &prgmInit) {
	const wchar_t *CFG_DIR = L"conf";
	sharedUtils::system::AutoCommandLine cmdParams;
	TrackedAdminValues admin;
	admin.configDir = dataDirBase + CFG_DIR;
	unique_ptr<Settings> configuration;
	try {
		admin.specified = ParseParam(admin.configFile, cmdParams.argc, cmdParams.argv, L"config", L"init.json");
		if(admin.configFile.empty()) throw std::exception("Specified config file is empty string!");
		sharedUtils::system::AutoGoDir<true> baseAppDir(dataDirBase.c_str(), true);
		sharedUtils::system::AutoGoDir<false> cfgDir(CFG_DIR, true);
		if(!cfgDir.Changed()) throw std::exception("Could not go to configuration directory");

		using namespace rapidjson;
		CFGLoadErrInfo failure;
		const std::wstring ori(admin.configFile);
		asizei attempt = admin.specified? 1 : 0;
		while(attempt < 2 && LoadConfigJSON(admin.loadedConfig.good, failure, admin.configFile)) {
			if(admin.loadedConfig.good.IsObject() == false) {
				admin.loadedConfig.good.SetNull();
				admin.loadedConfig.errCode = failure.errCode;
				admin.loadedConfig.errOff = failure.errOff;
				admin.loadedConfig.errDesc = std::move(failure.errDesc);
				admin.loadedConfig.raw = failure.raw.get();
				break;
			}
			else {
				admin.loadedConfig.valueErrors.clear();
				const Value::ConstMemberIterator redirect = admin.loadedConfig.good.FindMember("userConfiguration");
				if(attempt || redirect == admin.loadedConfig.good.MemberEnd() || redirect->value.IsString() == false) {
					configuration.reset(BuildSettings(admin.loadedConfig.valueErrors, admin.loadedConfig.good));
					break;
				}
				else {
					std::string utfbyte(redirect->value.GetString(), redirect->value.GetStringLength());
					std::wstring_convert< std::codecvt_utf8_utf16<wchar_t> > convert;
					admin.configFile = convert.from_bytes(utfbyte);
				}
			}
			attempt++;
		}
		admin.redirected = ori != admin.configFile;
		admin.valid = configuration != nullptr;
	} catch(std::string msg) {
		MessageBoxA(NULL, msg.c_str(), "M8M - Fatal early error", MB_OK);
		return false;
	} catch(std::wstring msg) {
		MessageBoxW(NULL, msg.c_str(), L"M8M - Fatal early error", MB_OK);
		return false;
	} catch(std::exception msg) {
		MessageBoxA(NULL, msg.what(), "M8M - Fatal early error", MB_OK);
		return false;
	}

	try {
		Network network;
		std::unique_ptr<Connections> remote;
		bool quit = false;
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
		WebMonitorTracker webMonitor(notify, network);
		WebAdminTracker webAdmin(notify, network);
		webMonitor.connectClicked = []() { LaunchBrowser(WEBMONITOR_PATH); };
		webAdmin.connectClicked = []() { LaunchBrowser(WEBADMIN_PATH); };
		asizei totalClients = 0;
		webAdmin.clientConnectionCallback = webMonitor.clientConnectionCallback = [&notify, &iconBitmaps, &totalClients](WebMonitorTracker::ClientConnectionEvent ev, aint change, asizei count) {
			switch(ev) {
			case WebMonitorTracker::cce_welcome: cout<<"--WS: New client connected."<<endl; break;
			case WebMonitorTracker::cce_farewell: cout<<"--WS: A websocket has just been destroyed."<<endl; break;
			}
			if(totalClients == 0 && change > 0) {
				std::unique_ptr<aubyte[]> ico(new aubyte[M8M_ICON_SIZE * M8M_ICON_SIZE * 4]);
				iconBitmaps.SetCurrentIcon(STATE_ICON_CLIENT_CONNECTED);
				iconBitmaps.GetCompositedIcon(ico.get());
				notify.SetIcon(ico.get(), M8M_ICON_SIZE, M8M_ICON_SIZE);
			}
			else if(totalClients > 0 && change < 0) {
				std::unique_ptr<aubyte[]> ico(new aubyte[M8M_ICON_SIZE * M8M_ICON_SIZE * 4]);
				iconBitmaps.SetCurrentIcon(STATE_ICON_LISTENING);
				iconBitmaps.GetCompositedIcon(ico.get());
				notify.SetIcon(ico.get(), M8M_ICON_SIZE, M8M_ICON_SIZE);
			}
			if(change > 0) totalClients++;
			else totalClients--;
			return true;
		};
		asizei listeningPorts = 0;
		webAdmin.serviceStateCallback = webMonitor.serviceStateCallback = [&notify, &iconBitmaps, &listeningPorts](bool listening) {
			if(listening) listeningPorts++;
			else listeningPorts--;
			if(listeningPorts == 0 || (listeningPorts == 1 && listening)) {
				std::array<aubyte, M8M_ICON_SIZE * M8M_ICON_SIZE * 4> ico;
				iconBitmaps.SetCurrentIcon(listening? STATE_ICON_LISTENING : STATE_ICON_NORMAL);
				iconBitmaps.GetCompositedIcon(ico);
				notify.SetIcon(ico.data(), M8M_ICON_SIZE, M8M_ICON_SIZE);
			}
		};
		notify.AddMenuItem(L"Open user app folder", [&dataDirBase]() { OpenFileExplorer(dataDirBase.c_str()); });
		notify.AddMenuSeparator();
		notify.ShowMessage(L"Getting ready!\nLeave me some seconds to warm up...");
		webAdmin.SetMessages(L"Enable web administration", L"Connect to web administration", L"Disable web admin");
		notify.AddMenuSeparator();
		webMonitor.SetMessages(L"Enable web monitor", L"Connect to web monitor", L"Disable web monitor");
		notify.AddMenuSeparator();
		notify.AddMenuSeparator();
		notify.AddMenuItem(L"Exit ASAP", [&quit]() { quit = true; });
		notify.BuildMenu();
		notify.Tick();

		bool firstShare = true;
		WebCommands parsers;
		std::unique_ptr<MinerInterface> processors;
		auto dispatchNWU([&processors](AbstractWorkSource &pool, stratum::AbstractWorkUnit *wu) {
			std::unique_ptr<stratum::AbstractWorkUnit> own(wu);
			processors->Mangle(pool, own);
		});
		auto onAllWorkersFailedAuth = []() {
			const wchar_t *title = L"No workers authorized!";
			const wchar_t *msg = L"Looks like no worker can successfully login to server.\n"
				L"This implies I can do nothing. I'll keep running so you can check the stats and config file.";
			MessageBox(NULL, msg, title, MB_ICONERROR);
		};
		remote.reset(InstanceConnections(configuration.get(), network, dispatchNWU));
		if(remote->GetNumPoolsAdded() == 0) {
			notify.ShowMessage(L"No remote servers could be found in current config.");
			//! \todo change icon to error icon
		}
		remote->SetNoAuthorizedWorkerCallback(onAllWorkersFailedAuth);
        TrackedValues stats(*remote, prgmInit.count());
		{
			WebServices quads(webMonitor, stats, webAdmin, admin);
			processors.reset(InstanceProcessingNodes(parsers, configuration.get(), quads, remote, sleepFunc));
		}

		ScopedFuncCall forceParserGoodbye([&parsers]() { parsers.clear(); }); // they must go away before the API wrapper bites the dust.
		if(configuration) {
			if(!processors->SetCurrentAlgo(configuration->algo.c_str(), configuration->impl.c_str())) {
				throw std::string("Algorithm \"") + configuration->algo + '.' + configuration->impl + ", not found.";
			}
			processors->AddSettings(configuration->implParams);
		}

		{
			asizei numDevices = 0, dummy;
			while(processors->GetDeviceConfig(dummy, numDevices)) numDevices++;
			stats.Resize(numDevices);
		}
		processors->Start();
        stats.minerStart = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		bool showShareStats = false;
		auto onShareResponse= [&showShareStats](bool ok) { showShareStats = true; };
		asizei sinceActivity = 0;
		std::vector<Network::SocketInterface*> toRead, toWrite;
		bool ignoreMinerTermination = configuration == nullptr || remote->GetNumServers() == 0, minerWorking = false;
		while(!quit) {
			if(admin.reloadRequested) {
				if(listeningPorts == 0) break;
				if(admin.reloadRequested == 1) {
					webMonitor.BeginClose();
					webAdmin.BeginClose();
				}
				admin.reloadRequested++;
			}
			notify.Tick();
			toRead.clear();
			toWrite.clear();
			remote->FillSleepLists(toRead, toWrite);
			webMonitor.FillSleepLists(toRead, toWrite);
			webAdmin.FillSleepLists(toRead, toWrite);
			asizei updated = 0;
			if(toRead.size() || toWrite.size()) { // typically 0 or 2 is true, 0 happens if no cfg loaded
				updated = network.SleepOn(toRead, toWrite, POLL_PERIOD_MS);
			}
			else sleepFunc(POLL_PERIOD_MS); //!< \todo perhaps I should leave this on and let this CPU gobble up resources so it can be signaled?
			if(!updated) {
				std::vector<Network::SocketInterface*> dummy;
				webMonitor.Refresh(dummy, dummy); // this will allow the server to shut down if necessary
				webAdmin.Refresh(dummy, dummy);
				if(toRead.size()) sinceActivity += POLL_PERIOD_MS;
				if(sinceActivity >= TIMEOUT_MS && configuration) { // if not configured, keep going on so user can set me up.
					// the timeout value has been selected according to a few comments in legacy miners.
					// It looks like stratum must send a notification every minute and they put a few watchdogs
					// timed 90secs so I am being super conservative here.
					throw std::exception("No activity from connections in 120 seconds. Fatal network fail? I give up");
				}
			}
			else {
				sinceActivity = 0;
				remote->Refresh(toRead, toWrite);
				webMonitor.Refresh(toRead, toWrite);
				webAdmin.Refresh(toRead, toWrite);
			}
			if(minerWorking == false && processors->Working()) {
				std::array<aubyte, M8M_ICON_SIZE * M8M_ICON_SIZE * 4> ico;
				iconBitmaps.SetCurrentState(STATE_OK);
				iconBitmaps.GetCompositedIcon(ico);
				notify.SetIcon(ico.data(), M8M_ICON_SIZE, M8M_ICON_SIZE);
				minerWorking = true;
			}
			std::string errorDesc;
			std::vector<MinerInterface::Nonces> sharesFound;
			if(processors->SharesFound(sharesFound)) {
				std::cout.setf(std::ios::dec);
				std::cout<<"Sending "<<sharesFound.size()<<" shares."<<std::endl;
				if(firstShare) {
					firstShare = false;
					notify.ShowMessage(L"Found my first share!\nNumbers are being crunched as expected.");
                    using namespace std::chrono;
                    stats.firstNonce = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
				}
				for(auto el = sharesFound.cbegin(); el != sharesFound.cend(); ++el) {
					stats.shareTime[el->deviceIndex].Took(el->scanPeriod, el->avgSLR);
					stats.shares[el->deviceIndex].lastResult = time(NULL); // seconds since epoch is fine.
					stats.shares[el->deviceIndex].good += el->nonces.size();
					stats.shares[el->deviceIndex].bad += el->bad;
					stats.shares[el->deviceIndex].stale += el->stale;
					std::cout<<"Device "<<el->deviceIndex<<" found a nonce."<<std::endl;
					{
						adouble took = el->scanPeriod.count() / 1000000.0;
						adouble rate = ((1 / took) * el->lastNonceScanAmount) / 1000.0;
						std::cout<<"Share time = "<<took<<" ( "<<auint(rate)<<" kh/s )"<<std::endl;
					}
					{
						std::chrono::microseconds inst, avg;
						stats.shareTime[el->deviceIndex].GetLast(inst, avg);
						adouble took = avg.count() / 1000000.0;
						adouble rate = ((1 / took) * el->lastNonceScanAmount) / 1000.0; // this does not make any sense.
						// ^ I should be tracking the number of hashes instead but it's still something.
						// ^ as long intensity is constant, this is correct anyway
						std::cout<<"Average time (since last) = "<<took<<" ( "<<auint(rate)<<" kh/s )"<<std::endl;
					}
					{
						adouble took = stats.shareTime[el->deviceIndex].GetAverage().count() / 1000000.0;
						adouble rate = ((1 / took) * el->lastNonceScanAmount) / 1000.0;
						std::cout<<"Average time (sliding window) = "<<took<<" ( "<<auint(rate)<<" kh/s )"<<std::endl;
					}
					{
						adouble took = stats.shareTime[el->deviceIndex].GetMin().count() / 1000000.0;
						adouble rate = ((1 / took) * el->lastNonceScanAmount) / 1000.0;
						std::cout<<"Min time = "<<took<<" ( "<<auint(rate)<<" kh/s )"<<std::endl;
					}
					{
						adouble took = stats.shareTime[el->deviceIndex].GetMax().count() / 1000000.0;
						adouble rate = ((1 / took) * el->lastNonceScanAmount) / 1000.0;
						std::cout<<"Max time = "<<took<<" ( "<<auint(rate)<<" kh/s )"<<std::endl;
					}
					std::cout<<"Shares found: "<<el->nonces.size()<<", bad: "<<el->bad<<", stale: "<<el->stale<<std::endl;
					asizei count = el->nonces.size();
					asizei sent = remote->SendShares(*el);
					stats.shares[el->deviceIndex].stale += count - sent;
					showShareStats = true;
				}
			}
			else if(!ignoreMinerTermination && processors->UnexpectedlyTerminated(errorDesc)) {
				auto makeWide = [](const std::string &blah) -> std::wstring {
					std::vector<wchar_t> chars(blah.length() + 1);
					for(asizei cp = 0; cp < blah.length(); cp++) chars[cp] = blah[cp];
					chars[blah.length()] = 0;
					return std::wstring(chars.data());
				};
				std::wstring ohno(L"Something caused hashing to fail.");
				if(errorDesc.empty()) ohno += L"\nIt seems to have failed big way, as I don't have any information on what happened!";
				else ohno += L" Apparent cause seems to be:\n\"" + makeWide(errorDesc) + L'"';
				ohno += L"\n\nPlease report this.";
				ohno += L"\nI'm doing nothing now. No numbers are getting processed, you probably just want to restart this program.";
				ohno += L"\nDo you want me to keep running (so you can check status)?";
				const wchar_t *title = L"Ouch!";
#if defined(_WIN32)
				if(IDNO == MessageBox(NULL, ohno.c_str(), title, MB_ICONERROR | MB_SYSTEMMODAL | MB_SETFOREGROUND | MB_YESNO)) return false;
#else
#error Do something else very invasive.
#endif
				ignoreMinerTermination = true;
			}
			if(showShareStats) {
				for(asizei p = 0; p < remote->GetNumServers(); p++) {
					const AbstractWorkSource &server(remote->GetServer(p));
					asizei sent = 0, rejected = 0, accepted= 0;
					for(asizei w = 0; w < server.GetNumUsers(); w++) {
						StratumState::WorkerNonceStats stat(server.GetUserShareStats(w));
						sent += stat.sent;
						rejected += stat.rejected;
						accepted += stat.accepted;
					}
					cout<<"pool \""<<server.name<<"\" sent accepted rejected "<<sent<<' '<<accepted<<' '<<rejected<<endl;
				}
				showShareStats = false;
			}
		}
	} catch(std::string msg) {
		MessageBoxA(NULL, msg.c_str(), "M8M - Fatal error", MB_OK);
	} catch(std::wstring msg) {
		MessageBoxW(NULL, msg.c_str(), L"M8M - Fatal early error", MB_OK);
	} catch(std::exception msg) {
		MessageBoxA(NULL, msg.what(), "M8M - Fatal error", MB_OK);
	}
	return admin.reloadRequested != 0;
}



#if defined(_WIN32)
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE unusedLegacyW16, PWSTR cmdLine, int showStatus) {
#else
int main(int argc, char **argv) {
#endif

#if defined(_WIN32) && (defined(_DEBUG) || defined(RELEASE_WITH_CONSOLE))
	sharedUtils::system::AutoConsole<false> handyOutputForDebugging;
	handyOutputForDebugging.Enable();
#endif
	
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

	std::wstring myAppData;
	{
	#if defined(_WIN32)
		PWSTR lappData = nullptr;
		HRESULT got = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &lappData);
		if(got != S_OK) {
			MessageBox(NULL, L"Could not locate %LOCALAPPDATA% directory.\nThis is a fatal error.", L"Directory error!", MB_ICONERROR | MB_SYSTEMMODAL | MB_SETFOREGROUND);
			return 0;
		}
		ScopedFuncCall clear([lappData]() { CoTaskMemFree(lappData); });
		myAppData = lappData;
		myAppData += DIR_SEPARATOR;
		myAppData += L"M8M" DIR_SEPARATOR;
	#else
	#error change to some working/settings directory for some OS
	#endif
	}

	std::function<void(auint sleepms)> sleepFunc = 
#if defined _WIN32
		[](auint ms) { Sleep(ms); };
#else
#error This OS needs a sleep function.
#endif

	IconCompositer<16, 16> iconBitmaps;
	try {
		const aubyte white[4] =  { 255, 255, 255, 255 };
		const aubyte green[4] =  {   0, 255,   0, 255 };
		const aubyte yellow[4] = {   0, 255, 255, 255 };
		const aubyte blue[4] =   { 255,   0,   0, 255 };
		const aubyte red[4] =    {   0,   0, 255, 255 };
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

    const auto prgmInitialized = std::chrono::system_clock::now();
	bool run = true;
	bool nap = false;
	while(run) {
		iconBitmaps.SetCurrentIcon(STATE_ICON_NORMAL);
		iconBitmaps.SetCurrentState(STATE_INIT);
		if(nap) sleepFunc(500); // reinforce the perception of rebooting the whole thing
		run = ProgramBody(sleepFunc, iconBitmaps, myAppData, std::chrono::duration_cast<std::chrono::seconds>(prgmInitialized.time_since_epoch()));
		nap = true;
	}
	return 0;
}
