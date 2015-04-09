/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "AbstractWSServer.h"


const auint AbstractWSServer::maxClients = 5;


void AbstractWSServer::FillSleepLists(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
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


void AbstractWSServer::Refresh(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
	if(!landing) return; // fully shut down --> no clients either
	PurgeClosedConnections();
	if(AreYouClosing() == false) {
		ReadWrite(toRead, toWrite);
		UpgradeConnect(toRead, toWrite);
		EnqueuePushData();
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
		ReadWrite(toRead, toWrite); // We still have to send all the close requests and get their confirms...

		// Oh wait! Above we played nice. But if a peer is not nice to us, we are not nice to it and kill TCP conn.
		if(shutdownInitiated < std::chrono::system_clock::now() - std::chrono::seconds(5)) {
			for(asizei loop = clients.size() - 1; loop < clients.size(); loop--) destroy(loop);
		}
		if(clients.size() == 0) { // Now all the clients are gone, shut down the listening socket.
			network.CloseServiceSocket(*landing);
			landing = nullptr;
			numberedPushers = 0;
			CloseCompleted();
			if(serviceStateCallback) serviceStateCallback(false);
		}
	}
}


void AbstractWSServer::Listen() {
	if(!landing) {
		landing = &network.NewServiceSocket(port, maxClients);	
		shutdownInitiated = TimePoint();
		if(serviceStateCallback) serviceStateCallback(true);
	}
}


void AbstractWSServer::ReadWrite(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
	// First thing to do: serve already connected websockets. We first consume, then send.
	struct ProcessingGuard {
		const Network::SocketInterface **mark;
		ProcessingGuard(const Network::SocketInterface **storage, const Network::SocketInterface *guard) {
			mark = storage;
			*mark = guard;
		}
		~ProcessingGuard() { *mark = nullptr; }
	};
	std::for_each(toRead.begin(), toRead.end(), [this](const Network::SocketInterface *skt) {
		auto el = std::find(clients.begin(), clients.end(), skt);
		if(el == clients.cend()) return; // either not mine or landing socket
		if(el->initializer) return; // Upgrading socket state later.
		std::vector<ws::Connection::Message> msg;
		if(!el->ws->Read(msg)) return;

		ProcessingGuard currentlyMangling(&this->processing, skt);
		for(asizei loop = 0; loop < msg.size(); loop++) {
			rapidjson::Document object;
			msg[loop].push_back(0); // string must be zero terminated for parse
			//! \todo figure out how to limit rapidjson parsing!
			object.ParseInsitu(reinterpret_cast<char*>(msg[loop].data()));
			if(object.HasParseError()) throw std::exception("Invalid JSON received.");
			rapidjson::Value::ConstMemberIterator cmdIter(object.FindMember("command"));
			if(cmdIter == object.MemberEnd() || cmdIter->value.IsString() == false) throw std::exception("Not a command object.");
			const rapidjson::Value &cmd(cmdIter->value);
			const std::string command(cmd.GetString(), cmd.GetStringLength());
			auto matched = commands.find(command.c_str());
			std::string reply;
			std::unique_ptr<commands::PushInterface> stream;
            if(matched == commands.cend()) reply = "!!ERROR: no such command \"" + command + '"';
            else if(matched->second->Mangle(reply, stream, object) == false) {
			    throw std::exception("Impossible, code out of sync. Command name already matched!");
            }
			if(!reply.length()) {
				throw std::exception("Invalid zero-length reply.");
			}
			if(stream) {
				auto list = std::find_if(pushing.begin(), pushing.end(), [&el](const PushList &test) { return test.dst == &el->conn.get(); });
				std::unique_ptr<NamedPush> dataPush(new NamedPush);
				dataPush->pusher = std::move(stream);
				if(matched->second->GetMaxPushing() > 1) dataPush->name = std::to_string(numberedPushers);
				else numberedPushers--;
				dataPush->originator = matched->second;
				if(list == pushing.end()) {
					PushList add;
					add.dst = &el->conn.get();
					add.active.push_back(std::move(dataPush));
					pushing.push_back(std::move(add));
				}
				else {
					if(list->active.size() + 1 == matched->second->GetMaxPushing()) {
						reply = "!!ERROR: max amount of pushers reached!!";
						numberedPushers--;
					}
					else list->active.push_back(std::move(dataPush));
				}
				numberedPushers++;
			}
			el->ws->EnqueueTextMessage(reply);
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
}


void AbstractWSServer::UpgradeConnect(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
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
		if(clients.size() >= maxClients || shutdownInitiated != TimePoint()) network.CloseConnection(pipe); // or maybe I could not even allow it - I would keep getting waken up
		else {
			ScopedFuncCall destroy([&pipe, this]() { network.CloseConnection(pipe); });
			std::unique_ptr<ws::HandShaker> init(new ws::HandShaker(pipe, wsProtocol.c_str(), resURI.c_str()));
			clients.push_back(ClientState(pipe));
			clients.back().initializer = std::move(init);
			destroy.Dont();
			if(this->clientConnectionCallback) this->clientConnectionCallback(cce_welcome, 1, clients.size());
		}
	}
}


void AbstractWSServer::Unsubscribe(const std::string &commandName, const std::string &stream) {
	const Network::SocketInterface *processing = this->processing;
	if(!processing) return; // impossible
	auto list = std::find_if(pushing.begin(), pushing.end(), [processing](const PushList &test) { return test.dst == processing; });
	if(list == pushing.end()) return; // this client had no pushes active
	auto cmdMatch = commands.find(commandName);
	if(cmdMatch == commands.cend()) return; // maybe this would be worth an exception?
	const commands::AbstractCommand *command = cmdMatch->second;
	for(asizei loop = 0; loop < list->active.size(); loop++) {
		if(list->active[loop]->originator != command) continue;
		if(stream.empty() || stream == list->active[loop]->name) {
			list->active.erase(list->active.begin() + loop);
			loop--;
			if(stream.length()) break;
		}
	}

}


void AbstractWSServer::PurgeClosedConnections() {
	// First of all, no matter what, get the rid of all sockets which are closed: they would piss off our logic big way.
	for(asizei loop = clients.size() - 1; loop < clients.size(); loop--) {
		bool garbage = clients[loop].ws && clients[loop].ws->GetStatus() == ws::Connection::wss_closed;
		garbage |= clients[loop].conn.get().Works() == false;
		if(garbage) {
			for(asizei rem = 0; rem < pushing.size(); rem++) {
				if(pushing[rem].dst == &clients[loop].conn.get()) {
					pushing.erase(pushing.begin() + rem);
					break;
				}
			}
			network.CloseConnection(clients[loop].conn);
			clients.erase(clients.begin() + loop);
			if(clientConnectionCallback) clientConnectionCallback(cce_farewell, -1, clients.size());
		}
	}
	//! \todo The handshake can also fail... but I have no way to know currently.
	//! In fact, I should consider it failing after a while even if the handshake is not really failing...
	//! What's the best thing to do here?
}


void AbstractWSServer::EnqueuePushData() {
	// Now give all the possibility to produce new data... which will never be sent if we closed but who cares!
	std::for_each(pushing.cbegin(), pushing.cend(), [this](const PushList &mangle) {
		using namespace rapidjson;
		for(asizei loop = 0; loop < mangle.active.size(); loop++) {
			Document send;
			if(mangle.active[loop]->pusher->Refresh(send)) {
				Document ret;
				ret.SetObject();
				const std::string &ori(mangle.active[loop]->originator->name);
				ret.AddMember("pushing", StringRef(ori.c_str()), ret.GetAllocator());
				if(mangle.active[loop]->originator->GetMaxPushing() > 1) ret.AddMember("stream", StringRef(mangle.active[loop]->name.c_str()), ret.GetAllocator());
				ret.AddMember("payload", send, ret.GetAllocator());
				auto sink = std::find_if(clients.cbegin(), clients.cend(), [&mangle](const ClientState &test) {
					return &test.conn.get() == mangle.dst && test.ws.get();
				});
				if(sink != clients.cend()) {
					rapidjson::StringBuffer pretty;
					#if _DEBUG
					rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(pretty, nullptr);
					#else
					rapidjson::Writer<rapidjson::StringBuffer> writer(pretty, nullptr);
					#endif
					ret.Accept(writer);
					sink->ws->EnqueueTextMessage(pretty.GetString(), pretty.GetSize());
				}
			}
		}
	});
}
