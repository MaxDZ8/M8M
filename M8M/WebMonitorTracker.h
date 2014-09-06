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
#include "commands/ExtensionState.h"
#include "commands/UnsubscribeCMD.h"


class WebMonitorTracker : public commands::UnsubscribeCMD::PusherOwnerInterface {
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
	asizei numberedPushers;

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

    struct NamedPush {
        std::string name;
		commands::AbstractCommand *originator;
        std::unique_ptr<commands::PushInterface> pusher;
		NamedPush() : originator(nullptr) { }
	};

	struct PushList {
		const Network::SocketInterface *dst; //!< not ConnectedSocketInterface as it must compare to wait list entry
		std::vector< std::unique_ptr<NamedPush> > active;

		PushList() : dst(nullptr) { }
		PushList(PushList &&other) : dst(other.dst), active(std::move(other.active)) { }
		PushList& operator=(PushList &&other) {
			if(this != &other) {
				dst = other.dst;
				active = std::move(other.active);
			}
			return *this;
		}
	};
	std::vector<PushList> pushing;

	bool EnableWeb();
	bool DisableWeb();

	void NormalNetworkIO(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);

	/*! This is to support unsubscribe as it has no way to know which client requested unsubscribe.
	Sure, I keep an unique string of stream identifiers but if there's no stream id there's no use for it.
	NormalNetworkIO sets this in a loop using an ad-hoc object so this is guaranteed to match the client owning commands or be nullptr. */
	const Network::SocketInterface *processing;

	void Unsubscribe(const std::string &command, const std::string &stream);

public:
	static aushort monitorPort;
	const static auint maxMonitors;
	std::map<std::string, commands::ExtensionState> extensions; //!< this shouldn't really be public because of the way internal state is managed but there's not much usefulness in making it private either...

	enum ClientConnectionEvent {
		cce_welcome, //!< sent as soon as TCP/IP connection completes - websocket handshake still going on
		cce_farewell //!< I just destroyed a socket.
	};
	std::function<bool(ClientConnectionEvent)> clientConnectionCallback;

	const wchar_t *browserURI;
	WebMonitorTracker(NotifyIcon &icon, NetworkInterface &netAPI, const wchar_t *webApp)
		: menu(icon), miON(0), miOFF(0), miCONN(0), network(netAPI), browserURI(webApp), landing(nullptr), numberedPushers(0), processing(nullptr) { }
	void SetMessages(const wchar_t *enable, const wchar_t *connect, const wchar_t *disable) {
		miOFF = menu.AddMenuItem(disable, [this]() { Toggle(); }, false);
		miCONN = menu.AddMenuItem(connect, [this]() { if(browserURI) LaunchBrowser(browserURI); }, false);
		miON = menu.AddMenuItem(enable, [this]() { Toggle(); });
	}
	void FillSleepLists(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);
	void Refresh(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);
	void RegisterCommand(commands::AbstractCommand &cmd) { commands.insert(std::make_pair(cmd.name, &cmd)); }
};
