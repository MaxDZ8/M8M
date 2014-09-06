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
#include <iostream>
#include "Connections.h"
#include "../Common/Init.h"
#include "AbstractThreadedMiner.h"
#include "AlgoFactories/QubitCL12.h"
#include <iomanip>
#include "../Common/NotifyIcon.h"
#include "M8MIcon.h"
#include "WebMonitorTracker.h"
#include "../Common/AREN/SharedUtils/AutoConsole.h"

#include "commands/Monitor/SystemInfoCMD.h"
#include "commands/Monitor/AlgoCMD.h"
#include "commands/Monitor/PoolCMD.h"
#include "commands/Monitor/DeviceConfigCMD.h"
#include "commands/Monitor/RejectReasonCMD.h"
#include "commands/Monitor/ConfigInfoCMD.h"
#include "commands/Monitor/ScanTime.h"
#include "commands/Monitor/DeviceShares.h"
#include "commands/Monitor/PoolShares.h"
#include "commands/ExtensionListCMD.h"
#include "commands/UnsubscribeCMD.h"
#include "commands/UpgradeCMD.h"
#include "commands/VersionCMD.h"

#include "TimedValueStream.h"

#include "../Common/AREN/SharedUtils/OSUniqueChecker.h"


Connections* InstanceConnections(const Settings &settings, Network &manager, Connections::DispatchCallback df) {
	return new Connections(manager, settings.pools.cbegin(), 1, df);
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

struct TrackedValues : commands::monitor::ScanTime::DeviceTimeProviderInterface, commands::monitor::DeviceShares::ValueSourceInterface, commands::monitor::PoolShares::ShareProviderInterface {
	std::vector<TimedValueStream> shareTime;
	std::vector<commands::monitor::DeviceShares::ShareStats> shares;
	const Connections &servers;

	TrackedValues(const Connections &src) : servers(src) { }

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

};


typedef std::vector< std::unique_ptr<commands::AbstractCommand> > WebCommands;


template<typename CreateClass, typename Param>
void SimpleCommand(WebCommands &persist, WebMonitorTracker &mon, Param &param) {
	std::unique_ptr<CreateClass> build(new CreateClass(param));
	mon.RegisterCommand(*build);
	persist.push_back(std::move(build));
}

template<typename MiningProcessorsProvider>
void RegisterMonitorCommands(WebCommands &persist, WebMonitorTracker &mon, AbstractMiner<MiningProcessorsProvider> &miner, const std::unique_ptr<Connections> &connections, TrackedValues &tracking) {
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
	{
		std::unique_ptr<commands::VersionCMD> build(new commands::VersionCMD());
		mon.RegisterCommand(*build);
		persist.push_back(std::move(build));
	}
	SimpleCommand<commands::ExtensionListCMD>(persist, mon, mon.extensions);
	SimpleCommand<commands::UpgradeCMD>(persist, mon, mon.extensions);
	SimpleCommand<commands::UnsubscribeCMD>(persist, mon, mon);
}


MinerInterface* InstanceProcessingNodes(WebCommands &persist, const Settings &settings, WebMonitorTracker &reg, const std::unique_ptr<Connections> &connections, TrackedValues &tracking) {
	std::function<void(auint sleepms)> sleepFunc = 
#if defined _WIN32
		[](auint ms) { Sleep(ms); };
#else
#error This OS needs a sleep function.
#endif

	if(!_stricmp("opencl", settings.driver.c_str()) || !_stricmp("ocl", settings.driver.c_str())) {
		std::vector< std::unique_ptr< AlgoFamily<OpenCL12Wrapper> > > algos;
		std::unique_ptr< AlgoFamily<OpenCL12Wrapper> > add;
		// Maybe this will come back from the dead a day in form of scrypt-n perhaps.
		// if(!_stricmp(settings.algo.c_str(), "scrypt1024")) add.reset(new Scrypt1024_CL12(true, ErrorsToSTDOUT));
		// else
		if(!_stricmp(settings.algo.c_str(), "qubit")) add.reset(new QubitCL12(true, ErrorsToSTDOUT));
		else throw std::string("Algorithm \"") + settings.algo + "\" not found in OpenCL 1.2 driver.";
		algos.push_back(std::move(add));
		std::unique_ptr< AbstractThreadedMiner<OpenCL12Wrapper> > ret(new AbstractThreadedMiner<OpenCL12Wrapper>(algos, sleepFunc));
		RegisterMonitorCommands(persist, reg, *ret, connections, tracking);
		ret->CheckNonces(settings.checkNonces);
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

#define DEFAULT_ADMIN_PORT 31001


#if 0
class WebAdminTracker {
	NotifyIcon &menu;
	asizei miON, miOFF, miCONN; // miON will be a sub-menu in the future.

	void Toggle() {
		if(!menu.ToggleMenuItemStatus(miON)) {
			std::cout<<"Activating web admin."<<std::endl;
		}
		menu.ToggleMenuItemStatus(miOFF);
		menu.ToggleMenuItemStatus(miOFF);
		if(!menu.ToggleMenuItemStatus(miCONN)) {
			std::cout<<"Disabling web admin."<<std::endl;
		}
	}

public:
	WebAdminTracker(NotifyIcon &icon) : menu(icon), miON(0), miOFF(0), miCONN(0) { }
	void SetMainMessages(const wchar_t *enable, const wchar_t *connect, const wchar_t *disable) {
		miON = menu.AddMenuItem(enable, [this]() { Toggle(); }); //!< \todo submenu, open for 5/15/forever minutes
		miCONN = menu.AddMenuItem(connect, []() { LaunchBrowser(WEBADMIN_PATH); }, false);
		miOFF = menu.AddMenuItem(disable, [this]() { Toggle(); }, false);
	}
};  
#endif // 0


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

	unique_ptr<Settings> configuration;
	try {
		Network network;
		std::unique_ptr<Connections> remote;
		//cout<<"M8M starting, loading =\""<<FLAGS_config<<'"'<<endl;
		bool quit = false;
		configuration.reset(LoadConfig(L"init.json"));
		NotifyIcon notify;
		WebMonitorTracker webMonitor(notify, network, WEBMONITOR_PATH);
		webMonitor.clientConnectionCallback = [](WebMonitorTracker::ClientConnectionEvent ev) {
			switch(ev) {
			case WebMonitorTracker::cce_welcome: cout<<"--WS: New client connected."<<endl; break;
			case WebMonitorTracker::cce_farewell: cout<<"--WS: A websocket has just been destroyed."<<endl; break;
			}
			return true;
		};
		////WebAdminTracker webAdmin(notify, network);
		notify.AddMenuSeparator();
		notify.SetIcon(L"M8M - An (hopefully) educational cryptocurrency miner.", M8M_NOTIFY_ICON_16X16, 16, 16);
		notify.ShowMessage(L"Getting ready!\nLeave me some seconds to warm up...");
		webMonitor.SetMessages(L"Enable web monitor", L"Connect to web monitor", L"Disable web monitor");
		notify.AddMenuSeparator();
		////webAdmin.SetMainMessages(L"Enable web administration", L"Connect to web administration", L"Disable web admin");
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
		remote.reset(InstanceConnections(*configuration, network, dispatchNWU));
		remote->SetNoAuthorizedWorkerCallback(onAllWorkersFailedAuth);
		TrackedValues stats(*remote);
		processors.reset(InstanceProcessingNodes(parsers, *configuration, webMonitor, remote, stats));

		ScopedFuncCall forceParserGoodbye([&parsers]() { parsers.clear(); }); // they must go away before the API wrapper bites the dust.
		if(!processors->SetCurrentAlgo(configuration->algo.c_str(), configuration->impl.c_str())) {
			throw std::string("Algorithm \"") + configuration->algo + '.' + configuration->impl + ", not found.";
		}
		processors->AddSettings(configuration->implParams);

		{
			asizei numDevices = 0, dummy;
			while(processors->GetDeviceConfig(dummy, numDevices)) numDevices++;
			stats.Resize(numDevices);
		}
		processors->Start();
		bool showShareStats = false;
		auto onShareResponse= [&showShareStats](bool ok) { showShareStats = true; };
		asizei sinceActivity = 0;
		std::vector<Network::SocketInterface*> toRead, toWrite;
		bool ignoreMinerTermination = false;
		while(!quit) {
			notify.Tick();
			toRead.clear();
			toWrite.clear();
			remote->FillSleepLists(toRead, toWrite);
			webMonitor.FillSleepLists(toRead, toWrite);
			asizei updated = network.SleepOn(toRead, toWrite, POLL_PERIOD_MS);
			if(!updated) {
				sinceActivity += POLL_PERIOD_MS;
				if(sinceActivity >= TIMEOUT_MS) {
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
			}
			std::string errorDesc;
			std::vector<MinerInterface::Nonces> sharesFound;
			if(processors->SharesFound(sharesFound)) {
				std::cout.setf(std::ios::dec);
				std::cout<<"Sending "<<sharesFound.size()<<" shares."<<std::endl;
				if(firstShare) {
					firstShare = false;
					notify.ShowMessage(L"Found my first share!\nNumbers are being crunched as expected.");
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
				if(IDNO == MessageBox(NULL, ohno.c_str(), title, MB_ICONERROR | MB_SYSTEMMODAL | MB_SETFOREGROUND | MB_YESNO)) return -1;
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
		///////////if(FLAGS_errorBox) MessageBoxA(NULL, msg.c_str(), "M8M - Fatal error", MB_OK);
		if(true) MessageBoxA(NULL, msg.c_str(), "M8M - Fatal error", MB_OK);
		else std::cout<<msg.c_str();
	} catch(std::exception msg) {
		///////////if(FLAGS_errorBox) MessageBoxA(NULL, msg.what(), "M8M - Fatal error", MB_OK);
		if(true) MessageBoxA(NULL, msg.what(), "M8M - Fatal error", MB_OK);
		else std::cout<<msg.what();
	}

	return 0;
}
