/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/Network.h"
#include "../Common/WebSocket/HandShaker.h"
#include "../Common/WebSocket/Connection.h"
#include <algorithm>
#include "commands/UnsubscribeCMD.h"
#include "commands/ExtensionState.h"
#include <chrono>


class AbstractWSServer : public commands::UnsubscribeCMD::PusherOwnerInterface {
public:
	const aushort port;
	const std::string resURI, wsProtocol;
	const static auint maxClients;

	AbstractWSServer(NetworkInterface &netAPI, aushort servicePort, const char *httpRes, const char *wsProtoString)
		: network(netAPI), landing(nullptr), numberedPushers(0), port(servicePort), processing(nullptr), resURI(httpRes), wsProtocol(wsProtoString) { }
	void FillSleepLists(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);
	void Refresh(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);
	void RegisterCommand(std::unique_ptr<commands::AbstractCommand> &cmd) {
        auto pair(std::make_pair(cmd->name, std::move(cmd)));
        commands.insert(std::move(pair));
    }
	/*! Create a socket and wait for new clients to connect. This operation is synchronous so there's no callback. */
	void Listen();
	bool AreYouListening() const { return landing != nullptr; }

	void BeginClose() { shutdownInitiated = std::chrono::system_clock::now(); }
	bool AreYouClosing() { return shutdownInitiated != TimePoint(); } //!< only relevant when AreYouListening is true

    asizei GetNumClients() const { return clients.size(); }

	/*! This function is called after a client connection has been dropped or added.
	The 1st parameter is 1 if a client was added, -1 if it was removed.
	2nd parameter is the number of clients currently connected. */
	std::function<void(aint change, asizei count)> clientConnectionCallback;

	/*! Shutting down the server takes some care as we must try to gracefully close.
	Therefore, this is called to signal close completed with parameter false.
	Opening the service instead is a much simplier operation, just call Listen and goodbye yet we call it anyway with parameter true
	as helper to outer code. */
	std::function<void(bool)> serviceStateCallback;

	/*! This should really be private but until I figure out what extensions are supposed to be, I keep it there. */
	std::map<std::string, commands::ExtensionState> extensions;

private:
	typedef std::chrono::time_point<std::chrono::system_clock> TimePoint;

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

	
	void ReadWrite(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);
	void UpgradeConnect(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);
	void Unsubscribe(const std::string &command, const std::string &stream);
	void PurgeClosedConnections();
	void EnqueuePushData();

	NetworkInterface &network;
	NetworkInterface::ServiceSocketInterface *landing; //!< A simple pointer is sufficient since the network blasts it anyway, we just have to de-register it.
	TimePoint shutdownInitiated; //!< if connections are not shut down after a few seconds, they get blasted, 0 --> running no shutdown requested
	asizei numberedPushers;
	std::vector<ClientState> clients;
	std::map<std::string, std::unique_ptr<commands::AbstractCommand>> commands;
	std::vector<PushList> pushing;
	/*! This is to support unsubscribe as it has no way to know which client requested unsubscribe.
	Sure, I keep an unique string of stream identifiers but if there's no stream id there's no use for it.
	NormalNetworkIO sets this in a loop using an ad-hoc object so this is guaranteed to match the client owning commands or be nullptr. */
	const Network::SocketInterface *processing;	
};
