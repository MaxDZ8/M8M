/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "WebMonitorTracker.h"


aushort WebMonitorTracker::monitorPort = 31000;
const auint WebMonitorTracker::maxMonitors = 5;


bool WebMonitorTracker::EnableWeb() {
	if(landing) {
		menu.ShowMessage(L"Previous server is still shutting down.\nTry again later."); //!< \todo warning here
		return false;
	}
	landing = &network.NewServiceSocket(monitorPort, maxMonitors);
	shutdownInitiated = TimePoint();
	return true;
}


bool WebMonitorTracker::DisableWeb() {
	if(!landing) {
		menu.ShowMessage(L"Server already shut down."); // I don't think that's even possible!
		return false;
	}
	if(shutdownInitiated == TimePoint()) shutdownInitiated = std::chrono::system_clock::now();
	return true;
}


void WebMonitorTracker::NormalNetworkIO(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
	// First thing to do: serve already connected websockets. We first consume, then send.
	std::for_each(toRead.begin(), toRead.end(), [this](const Network::SocketInterface *skt) {
		auto el = std::find(clients.begin(), clients.end(), skt);
		if(el == clients.cend()) return; // either not mine or landing socket
		if(el->initializer) return; // Upgrading socket state later.
		std::vector<ws::Connection::Message> msg;
		if(!el->ws->Read(msg)) return;

		Json::Reader parser;
		for(asizei loop = 0; loop < msg.size(); loop++) {
			const char *begin = reinterpret_cast<const char*>(msg[loop].data());
			const char *limit = reinterpret_cast<const char*>(msg[loop].data() + msg[loop].size());
			Json::Value object;
			if(!parser.parse(begin, limit, object)) throw std::exception("Invalid JSON received.");
			if(object["command"].isString() == false) throw std::exception("Not a command object.");
			const std::string command(object["command"].asString());
			auto matched = commands.find(command.c_str());
			if(matched == commands.cend()) {
				if(object["push"].isNull()) { // this must be a command, but we don't support this.
					el->ws->EnqueueTextMessage(std::string("!!ERROR: unsupported command!!"));
					continue;
				}
				else {
					// Maybe some streamer can consume this?
					auto pusher = std::find_if(pushing.begin(), pushing.end(), [skt](const PushList &test) { return test.source == skt; });
					if(pusher == pushing.cend()) throw std::exception("Invalid command."); //!< \todo this is most definetely brittle!
					using namespace commands;
					bool processed = false;
					for(asizei mangler = 0; mangler < pusher->active.size(); mangler++) {
						std::string reply;
						PushInterface::ReplyAction result = pusher->active[mangler]->Mangle(reply, object);
						switch(result) {
						case PushInterface::ra_delete: {
							pusher->active.erase(pusher->active.begin() + mangler);
							processed = true;
							break;
						}
						case PushInterface::ra_consumed:
							if(reply.length()) el->ws->EnqueueTextMessage(reply);
							processed = true;
						}
					}
					if(processed) continue;
					throw std::exception("Unmatched command, but no PUSH matched either!");
				}
			}
			std::string reply;
			std::unique_ptr<commands::PushInterface> stream(matched->second->Mangle(reply, object));
			if(stream) {
				auto list = std::find_if(pushing.begin(), pushing.end(), [&el](const PushList &test) { return test.source == &el->conn.get(); });
				if(list == pushing.end()) {
					PushList add;
					add.source = &el->conn.get();
					add.active.push_back(std::move(stream));
					pushing.push_back(std::move(add));
				}
				else list->active.push_back(std::move(stream));
			}
			if(reply.length()) el->ws->EnqueueTextMessage(reply);
		}
	});
	std::for_each(toWrite.begin(), toWrite.end(), [this](const Network::SocketInterface *skt) {
		auto el = std::find(clients.begin(), clients.end(), skt);
		if(el == clients.cend()) return;
		if(el->initializer) return;
		if(el->ws->NeedsToSend()) el->ws->Send();
	});
	// Now same read-write thing for sockets handshaking. I do this the other way around.
	for(asizei loop = clients.size() - 1; loop < clients.size(); loop--) {
		ClientState &client(clients[loop]);
		auto r = std::find(toRead.begin(), toRead.end(), &client.conn.get());
		auto w = std::find(toWrite.begin(), toWrite.end(), &client.conn.get());
		if(r == toRead.end() && w == toWrite.end()) continue;
		if(client.ws) continue; // this one is for HTTP->WS upgrade, already mangled above
		else {
			if(r != toRead.end()) client.initializer->Receive();
			if(w != toWrite.end()) client.initializer->Send();
		}
	};
	std::for_each(clients.begin(), clients.end(), [this](ClientState &client) {
		if(client.initializer && client.initializer->Upgraded()) {
			client.ws.reset(new ws::Connection(client.conn, true));
			client.initializer.reset();
		}
	});
	// Last pick up new connections (only if not shutting down).
	auto newPeer = std::find(toRead.begin(), toRead.end(), landing);
	if(newPeer != toRead.end()) {
		auto &pipe(network.BeginConnection(*landing));
		if(clients.size() >= MAX_MONITORS || shutdownInitiated != TimePoint()) network.CloseConnection(pipe); // or maybe I could not even allow it - I would keep getting waken up
		else {
			ScopedFuncCall destroy([&pipe, this]() { network.CloseConnection(pipe); });
			std::unique_ptr<ws::HandShaker> init(new ws::HandShaker(pipe, "M8M-monitor", "monitor"));
			clients.push_back(ClientState(pipe));
			clients.back().initializer = std::move(init);
			destroy.Dont();
			if(this->clientConnectionCallback) this->clientConnectionCallback(cce_welcome);
		}
	}
}


void WebMonitorTracker::FillSleepLists(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
	if(!landing) return;  // it is always shut down after everything else so if that happens nothing is there for sure.
	if(shutdownInitiated == TimePoint()) toRead.push_back(landing); // not going to accept this anymore if shutting down
	std::for_each(clients.cbegin(), clients.cend(), [&toRead, &toWrite](const ClientState &ua) {
		if(ua.initializer) {
			if(ua.initializer->NeedsToSend()) toWrite.push_back(&ua.conn.get());
			else toRead.push_back(&ua.conn.get());
		}
		else if(ua.ws) {
			if(ua.ws->NeedsToSend()) toWrite.push_back(&ua.conn.get());
			else toRead.push_back(&ua.conn.get());
		}
	});
}


void WebMonitorTracker::Refresh(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
	if(!landing) return;
	{ // First of all, no matter what, get the rid of all sockets which are closed: they would piss off our logic big way.
		for(asizei loop = clients.size() - 1; loop < clients.size(); loop--) {
			bool garbage = clients[loop].ws && clients[loop].ws->GetStatus() == ws::Connection::wss_closed;
			garbage |= clients[loop].conn.get().Works() == false;
			if(garbage) {
				for(asizei rem = 0; rem < pushing.size(); rem++) {
					if(pushing[rem].source == &clients[loop].conn.get()) {
						pushing.erase(pushing.begin() + rem);
						break;
					}
				}
				network.CloseConnection(clients[loop].conn);
				clients.erase(clients.begin() + loop);
				if(clientConnectionCallback) clientConnectionCallback(cce_farewell);
			}
		}
		//! \todo The handshake can also fail... but I have no way to know currently.
		//! In fact, I should consider it failing after a while even if the handshake is not really failing...
		//! What's the best thing to do here?
	}
	if(shutdownInitiated == TimePoint()) {
		NormalNetworkIO(toRead, toWrite);
		// Now give all the possibility to produce new data... which will never be sent if we closed but who cares!
		std::for_each(pushing.cbegin(), pushing.cend(), [this](const PushList &mangle) {
			for(asizei loop = 0; loop < mangle.active.size(); loop++) {
				std::string send;
				mangle.active[loop]->Refresh(send);
				if(send.length()) {
					auto sink = std::find_if(clients.cbegin(), clients.cend(), [&mangle](const ClientState &test) {
						return &test.conn.get() == mangle.source && test.ws.get();
					});
					if(sink != clients.cend()) sink->ws->EnqueueTextMessage(send);
				}
			}
		});
	}
	else {
		pushing.clear();
		auto destroy = [this](asizei &index) {
			clients[index].ws.reset();
			network.CloseConnection(clients[index].conn);
			clients.erase(clients.begin() + index);
			index--;
		};
		for(asizei loop = 0; loop < clients.size(); loop++) {
			ClientState &client(clients[loop]);
			if(client.initializer) {
				client.initializer.reset();
				network.CloseConnection(client.conn);
				clients.erase(clients.begin() + loop);
				loop--;
			}
			else { // client.ws
				switch(client.ws->GetStatus()) {
				case ws::Connection::wss_operational: client.ws->EnqueueClose(ws::Connection::cr_away); break;
				case ws::Connection::wss_closed: destroy(loop); break;
				}
			}
		}
		// We still have to send all the close requests and get their confirms...
		NormalNetworkIO(toRead, toWrite); // not quite normal here!
		// What if a push gets requested while we are shutting down?
		// I should really sit down and write a serious protocol specification!

		// Oh wait! Above we played nice. But if a peer is not nice to us, we are not nice to it and kill TCP conn.
		if(shutdownInitiated < std::chrono::system_clock::now() - std::chrono::seconds(5)) {
			for(asizei loop = clients.size() - 1; loop < clients.size(); loop--) destroy(loop);
		}
		if(clients.size() == 0) { // Now all the clients are gone, shut down the listening socket.
			network.CloseServiceSocket(*landing);
			landing = nullptr;
		}
	}
}
