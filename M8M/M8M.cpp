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
#include "../Common/SourcePolicies/FirstPoolWorkSource.h"
#include "../Common/Exceptions.h"
#include "../Common/Init.h"
#include "DirectComputeAlgorithms.h"
#include "AbstractThreadedMiner.h"
#include "AlgoFactories/Scrypt1024_CL.h"
#include "AlgoFactories/QubitCL12.h"
#include <iomanip>
#include "../Common/NotifyIcon.h"
#include "M8MIcon.h"


DECLARE_string(config);
DEFINE_bool(enumerate, false, "Specifying this will cause the miner to dump information about available processors and then shutdown. Use this to get processor IDs to bind to workers.");
DEFINE_bool(errorBox, true, "Setting this to false will suppress exception messages going to error box. The program will therefore exit immediately. A good idea for dedicated miners.");

using std::cout;
using std::endl;
using std::unique_ptr;



/*! One object of this kind is used to manage proper creation of socket resources
and their relative pools. It's an helper object, effectively part of main and not very
separated in terms of responsabilities and management as it's really meant to only give
readability benefits. */
class Connections {
public:
	/*! Those two lists are rebuilt every time WaitForData is called. */
	std::vector<Network::SocketInterface*> toRead, toWrite;
	typedef std::function<void(AbstractWorkSource &pool, const stratum::WorkUnit&)> DispatchCallback;

	Connections(Network &factory, std::vector< std::unique_ptr<PoolInfo> >::const_iterator &first, asizei count, DispatchCallback df)
		: network(factory), routes(count), toRead(count), toWrite(count), dispatchFunc(df) {
		for(asizei loop = 0; loop < count; loop++) {
			const PoolInfo &conf(*first->get());
			const char *host = conf.host.c_str();
			const char *port = conf.explicitPort.length()? conf.explicitPort.c_str() : conf.service.c_str();
			Network::ConnectedSocketInterface *conn = &factory.BeginConnection(host, port);
			ScopedFuncCall clearConn([&factory, conn] { factory.CloseConnection(*conn); });
			std::unique_ptr<FirstPoolWorkSource> stratum(new FirstPoolWorkSource("M8M/DEVEL", conf, *conn));
			stratum->errorCallback = [](asizei i, int errorCode, const std::string &message) {
				cout<<"Stratum message ["<<std::to_string(i)<<"] generated error response by server (code "
					<<std::dec<<errorCode<<"=0x"<<std::hex<<errorCode<<std::dec<<"), server says: \""<<message<<"\""<<std::endl;
			};
			routes[loop] = Remote(conn, stratum.release());
			clearConn.Dont();
		}
	}
	~Connections() {
		for(asizei loop = 0; loop < routes.size(); loop++) {
			if(routes[loop].pool) routes[loop].pool->Shutdown();
			if(routes[loop].connection) network.CloseConnection(*routes[loop].connection);
		}
	}
	void PrepareSleepLists() {
		toRead.resize(0);
		toWrite.resize(0);
		for(asizei loop = 0; loop < routes.size(); loop++) {
			if(routes[loop].pool->NeedsToSend()) toWrite.push_back(routes[loop].connection);
			else toRead.push_back(routes[loop].connection);
		}
	}
	/*! Check all the currently managed pools. If a pool can either send or receive data,
	give it the chance of doing so. Call this after the .toRead and .toWrite lists have been updated. */
	void Refresh() {
		if(routes.size() == 0) throw std::exception("No active connections to pools, giving up.");
		for(asizei loop = 0; loop < routes.size(); loop++) {
			auto r = std::find(toRead.cbegin(), toRead.cend(), routes[loop].connection);
			auto w = std::find(toWrite.cbegin(), toWrite.cend(), routes[loop].connection);
			bool canRead = r != toRead.cend();
			bool canWrite = w != toWrite.cend();
			if(canRead || canWrite) {
				cout<<"Pool "<<loop<<" ";
				if(canRead) cout<<'R';
				if(canWrite) cout<<'W';
				cout<<endl;
				AbstractWorkSource &pool(*routes[loop].pool);
				auto happens = pool.Refresh(canRead, canWrite);
				switch(happens) {
				case AbstractWorkSource::e_nop: 
				case AbstractWorkSource::e_gotRemoteInput:
					break;
				case AbstractWorkSource::e_newWork: dispatchFunc(pool, pool.GenWorkUnit()); break;
				default:
					cout<<"Unrecognized pool event: "<<happens;
				}
			}
		}
	}
	void Close(Network::SocketInterface *bad) {
		for(asizei loop = 0; loop < routes.size(); loop++) {
			if(bad != routes[loop].connection) continue;
			Network::ConnectedSocketInterface *shutdown = routes[loop].connection;
			cout<<"Shutting down connection for pool "<<routes[loop].pool->GetName();
			for(asizei back = loop + 1; back < routes.size(); back++) routes[back - 1] = std::move(routes[back]);
			network.CloseConnection(*shutdown);
		}
		routes.pop_back();
	}
	/*! Send a bunch of shares to the pool matching by job id.
	\todo This is a bit easy-going as there's no guarantee job ids are going to be unique across different pools. I'll have to fix it in the future. */
	asizei SendShares(const MinerInterface::Nonces &found) {
		asizei enqueued = 0;
		for(asizei loop = 0; loop < routes.size(); loop++) {
			if(routes[loop].pool.get() != found.owner) continue;
			routes[loop].pool->Shares(found.job, found.nonce2, found.nonces);
			enqueued++;
		}
		return enqueued;
	}

private:
	Network &network;
	DispatchCallback dispatchFunc;
	struct Remote {
		Network::ConnectedSocketInterface *connection;
		std::unique_ptr<AbstractWorkSource> pool;
		Remote(Network::ConnectedSocketInterface *pipe = nullptr, AbstractWorkSource *own = nullptr) : connection(pipe), pool(own) { }
		Remote(Remote &&ori) {
			connection = ori.connection;
			pool = std::move(ori.pool);
		}
		Remote& operator=(Remote &&other) {
			if(this == &other) return other;
			connection = other.connection; // those are not owned so no need to "really" move them
			pool = std::move(other.pool);
			return *this;
		}
	};
	/*! This list is untouched, set at ctor and left as is. */
	std::vector<Remote> routes;

};


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


MinerInterface* InstanceProcessingNodes(const Settings &settings) {
	std::function<void(auint sleepms)> sleepFunc = 
#if defined _WIN32
		[](auint ms) { Sleep(ms); };
#else
#error This OS needs a sleep function.
#endif

	if(!_stricmp("opencl", settings.driver.c_str()) || !_stricmp("ocl", settings.driver.c_str())) {
		std::vector< std::unique_ptr< AlgoFamily<OpenCL12Wrapper> > > algos;
		std::unique_ptr< AlgoFamily<OpenCL12Wrapper> > add;
		if(!_stricmp(settings.algo.c_str(), "scrypt1024")) add.reset(new Scrypt1024_CL12(true, ErrorsToSTDOUT));
		else if(!_stricmp(settings.algo.c_str(), "qubit")) add.reset(new QubitCL12(true, ErrorsToSTDOUT));
		else throw std::string("Algorithm \"") + settings.algo + "\" not found in OpenCL 1.2 driver.";
		algos.push_back(std::move(add));
		std::unique_ptr< AbstractThreadedMiner<OpenCL12Wrapper> > ret(new AbstractThreadedMiner<OpenCL12Wrapper>(algos, sleepFunc));
		ret->CheckNonces(settings.checkNonces);
		return ret.release();
	}
	throw std::exception("Driver string invalid.");
}


void DumpDeviceInformations(MinerInterface &nodes) {
	cout<<"Enumeration functionalities to be implemented."<<endl;
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


#if defined(_WIN32)
#include <shellapi.h>
#define DIR_SEPARATOR L"\\"
#endif


#define PATH_TO_WEBAPPS L".." DIR_SEPARATOR L"web" DIR_SEPARATOR

#define WEBMONITOR_PATH PATH_TO_WEBAPPS L"monitor.html"
#define WEBADMIN_PATH PATH_TO_WEBAPPS L"admin.html"



void LaunchBrowser(const wchar_t *what) {
#if defined(_WIN32)
	SHELLEXECUTEINFO exe;
	memset(&exe, 0, sizeof(exe));
	exe.cbSize = sizeof(exe);
	exe.fMask = SEE_MASK_NOASYNC;// | SEE_MASK_CLASSNAME;
	exe.lpFile = what;
	exe.nShow = SW_SHOWNORMAL;
	//exe.lpClass = L"http";

	wchar_t buff[256];
	_wgetcwd(buff, 256);
	std::wcout<<buff<<std::endl;

	ShellExecuteEx(&exe);	
	if(reinterpret_cast<int>(exe.hInstApp) < 32) throw std::exception("Could not run browser.");
#else
#error what to do here?
#endif
}


class WebMonitorTracker {
	NotifyIcon &menu;
	asizei miON, miOFF, miCONN;

	void Toggle() {
		if(!menu.ToggleMenuItemStatus(miON)) {
			std::cout<<"Activating web monitor."<<std::endl;
		}
		menu.ToggleMenuItemStatus(miOFF);
		menu.ToggleMenuItemStatus(miOFF);
		if(!menu.ToggleMenuItemStatus(miCONN)) {
			std::cout<<"Disabling web monitor."<<std::endl;
		}
	}

public:
	WebMonitorTracker(NotifyIcon &icon) : menu(icon), miON(0), miOFF(0), miCONN(0) { }
	void SetMessages(const wchar_t *enable, const wchar_t *connect, const wchar_t *disable) {
		miON = menu.AddMenuItem(enable, [this]() { Toggle(); });
		miCONN = menu.AddMenuItem(connect, [this]() { LaunchBrowser(WEBMONITOR_PATH); }, false);
		miOFF = menu.AddMenuItem(disable, [this]() { Toggle(); }, false);

	}
};


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


int main(int argc, char **argv) {
	google::ParseCommandLineFlags(&argc, &argv, true);
	unique_ptr<Settings> configuration;
	try {
		cout<<"M8M starting, loading =\""<<FLAGS_config<<'"'<<endl;
		bool quit = false;
		configuration.reset(LoadConfig());
		NotifyIcon notify;
		WebMonitorTracker webMonitor(notify);
		WebAdminTracker webAdmin(notify);
		notify.SetIcon(L"M8M - An (hopefully) educational cryptocurrency miner.", M8M_NOTIFY_ICON_16X16, 16, 16);
		notify.ShowMessage(L"Getting ready!\nLeave me some seconds to warm up...");
		webMonitor.SetMessages(L"Enable web monitor", L"Connect to web monitoring", L"Disable web monitor");
		notify.AddMenuSeparator();
		webAdmin.SetMainMessages(L"Enable web administration", L"Connect to web administration", L"Disable web admin");
		notify.AddMenuSeparator();
		notify.AddMenuItem(L"Exit ASAP", [&quit]() { quit = true; });
		notify.BuildMenu();
		notify.Tick();

		Network network;
		bool firstShare = true;
		std::unique_ptr<MinerInterface> processors(InstanceProcessingNodes(*configuration));
		if(FLAGS_enumerate) {
			DumpDeviceInformations(*processors);
			return 0;
		}
		const asizei algoIndex = processors->EnableAlgorithm(configuration->algo.c_str(), configuration->impl.c_str(), configuration->implParams);
		if(algoIndex == asizei(-1)) throw std::string("Fatal error: algorithm \"") + configuration->algo.c_str() + "\" with implementation \"" + configuration->impl.c_str() + "\" is not available.";
		
		auto dispatchNWU([&processors, algoIndex](AbstractWorkSource &pool, const stratum::WorkUnit &wu) {
			processors->Mangle(pool, wu, algoIndex);
		});
		std::unique_ptr<Connections> remote(InstanceConnections(*configuration, network, dispatchNWU));
		asizei sinceActivity = 0;
		while(!quit) {
			notify.Tick();
			remote->PrepareSleepLists();
			asizei updated = network.SleepOn(remote->toRead, remote->toWrite, POLL_PERIOD_MS);
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
				for(asizei loop = 0; loop < remote->toRead.size(); loop++) {
					if(remote->toRead[loop]->Works() == false) remote->Close(remote->toRead[loop]);
				}
				remote->Refresh();
			}
			std::vector<MinerInterface::Nonces> sharesFound;
			if(processors->SharesFound(sharesFound)) {
				std::cout<<"Sending "<<std::dec<<sharesFound.size()<<" shares."<<std::endl;
				if(firstShare) {
					firstShare = false;
					notify.ShowMessage(L"Found my first share!\nNumbers are being crunched as expected.");
				}
				for(auto el = sharesFound.cbegin(); el != sharesFound.cend(); ++el) remote->SendShares(*el);
			}
		}
	} catch(std::string msg) {
		if(FLAGS_errorBox) MessageBoxA(NULL, msg.c_str(), "Fatal error", MB_OK);
		else std::cout<<msg.c_str();
	} catch(std::exception msg) {
		if(FLAGS_errorBox) MessageBoxA(NULL, msg.what(), "Fatal error", MB_OK);
		else std::cout<<msg.what();
	} catch(Exception *msg) {
		if(FLAGS_errorBox) MessageBoxA(NULL, "numbered exception (deprecated)", "Fatal error", MB_OK);
		else std::cout<<"numbered exception (deprecated)";
		delete msg;
	}
	return 0;
}
