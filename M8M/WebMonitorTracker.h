/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/NotifyIcon.h"
#include "../Common/Network.h"
#include "../Common/WebSocket/HandShaker.h"
#include "../Common/WebSocket/Connection.h"
#include "../Common/launchBrowser.h"
#include <algorithm>
#include "commands/AbstractCommand.h"


class WebMonitorTracker {
	NotifyIcon &menu;
	asizei miON, miOFF, miCONN;
	typedef std::chrono::time_point<std::chrono::system_clock> TimePoint;

	void Toggle() {
		const bool running = menu.GetMenuItemStatus(miON) == false;
		if(!running) {
			if(EnableWeb()) {
				menu.SetMenuItemStatus(miON, false);
				menu.SetMenuItemStatus(miCONN, true);
				menu.SetMenuItemStatus(miOFF, true);
			}
		}
		else {
			if(DisableWeb()) {
				menu.SetMenuItemStatus(miON, true);
				menu.SetMenuItemStatus(miCONN, false);
				menu.SetMenuItemStatus(miOFF, false);
			}
		}
	}
	NetworkInterface &network;
	NetworkInterface::ServiceSocketInterface *landing; //!< A simple pointer is sufficient since the network blasts it anyway, we just have to de-register it.
	TimePoint shutdownInitiated; //!< if connections are not shut down after a few seconds, they get blasted 0--> running

	struct ClientState {
		std::reference_wrapper<NetworkInterface::ConnectedSocketInterface> conn;
		std::unique_ptr<ws::HandShaker> initializer; //!< \note Perhaps I should limit this to one initializer per host as in the WS spec. One singleton?
		std::unique_ptr<ws::Connection> ws;
		ClientState(NetworkInterface::ConnectedSocketInterface &tcp) : conn(tcp) { }
		ClientState(ClientState &&other) : conn(other.conn) {
			initializer = std::move(other.initializer);
			ws = std::move(other.ws);
		}
		ClientState& operator=(ClientState &&other) {
			if(&other != this) {
				conn = std::move(other.conn);
				initializer = std::move(other.initializer);
				ws = std::move(other.ws);
			}
			return *this;
		}
		operator NetworkInterface::SocketInterface*() { return &conn.get(); }
	};

	std::vector<ClientState> clients;
	std::map<std::string, commands::AbstractCommand*> commands;

	struct PushList {
		const Network::SocketInterface *source; //!< not ConnectedSocketInterface as it must compare to wait list entry
		std::vector< std::unique_ptr<commands::PushInterface> > active;

		PushList() : source(nullptr) { }
		PushList(PushList &&other) : source(other.source), active(std::move(other.active)) { }
		PushList& operator=(PushList &&other) {
			if(this != &other) {
				source = other.source;
				active = std::move(other.active);
			}
			return *this;
		}
	};
	std::vector<PushList> pushing;

	bool EnableWeb();
	bool DisableWeb();

	void NormalNetworkIO(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);

public:
	static aushort monitorPort;
	const static auint maxMonitors;

	enum ClientConnectionEvent {
		cce_welcome, //!< sent as soon as TCP/IP connection completes - websocket handshake still going on
		cce_farewell //!< I just destroyed a socket.
	};
	std::function<bool(ClientConnectionEvent)> clientConnectionCallback;

	const wchar_t *browserURI;
	WebMonitorTracker(NotifyIcon &icon, NetworkInterface &netAPI, const wchar_t *webApp) : menu(icon), miON(0), miOFF(0), miCONN(0), network(netAPI), browserURI(webApp), landing(nullptr) { }
	void SetMessages(const wchar_t *enable, const wchar_t *connect, const wchar_t *disable) {
		miOFF = menu.AddMenuItem(disable, [this]() { Toggle(); }, false);
		miCONN = menu.AddMenuItem(connect, [this]() { if(browserURI) LaunchBrowser(browserURI); }, false);
		miON = menu.AddMenuItem(enable, [this]() { Toggle(); });
	}
	void FillSleepLists(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);
	void Refresh(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);
	void RegisterCommand(commands::AbstractCommand &cmd) { commands.insert(std::make_pair(cmd.name, &cmd)); }
};
