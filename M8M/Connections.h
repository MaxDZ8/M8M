/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/SourcePolicies/FirstPoolWorkSource.h"
#include <iostream>
#include "NonceStructs.h"

using std::cout;
using std::endl;
using std::unique_ptr;

/*! One object of this kind is used to manage proper creation of socket resources
and their relative pools. It's an helper object, effectively part of main and not very
separated in terms of responsabilities and management as it's really meant to only give
readability benefits. */
class Connections {
public:
	typedef std::function<void(AbstractWorkSource &pool, std::unique_ptr<stratum::AbstractWorkFactory> &newWork)> DispatchCallback;
	typedef std::function<void(AbstractWorkSource &pool, stratum::WorkDiff &diff)> DiffChangeCallback;
	DispatchCallback dispatchFunc;
    DiffChangeCallback diffChangeFunc;

    typedef std::function<void(const AbstractWorkSource &pool)> ActivityCallback;
    ActivityCallback onPoolCommand; //!< if provided, call this every time a certain pool receives something terminated with newline, (maybe not a command in protocol sense)

	Connections(Network &factory) : network(factory), addedPoolCount(0) {
        dispatchFunc = [](AbstractWorkSource &, std::unique_ptr<stratum::AbstractWorkFactory>&) { }; // better to nop this rather than checking
        diffChangeFunc = [](AbstractWorkSource &, stratum::WorkDiff &) { };
    }
	~Connections() {
		for(asizei loop = 0; loop < routes.size(); loop++) {
			if(routes[loop].pool) routes[loop].pool->Shutdown();
			if(routes[loop].connection) network.CloseConnection(*routes[loop].connection);
		}
	}
	void FillSleepLists(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
		toRead.resize(0);
		toWrite.resize(0);
		for(asizei loop = 0; loop < routes.size(); loop++) {
			if(routes[loop].pool->NeedsToSend()) toWrite.push_back(routes[loop].connection);
			else toRead.push_back(routes[loop].connection);
		}
	}
	/*! Check all the currently managed pools. If a pool can either send or receive data,
	give it the chance of doing so. You do this by passing a list of sockets which got input, the function will filter by itself. */
	void Refresh(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
		if(routes.size() == 0) return;
		
		for(asizei loop = 0; loop < toRead.size(); loop++) { // First of all, destroy routes which have gone down.
			NetworkInterface::SocketInterface *test = toRead[loop];
			auto goner = std::find_if(routes.begin(), routes.end(), [test](const Remote &server) { return server.connection == test; });
			if(goner != routes.end() && test->Works() == false) {
                std::string name(routes[loop].pool->name.length()? routes[loop].pool->name : ("[" + std::to_string(loop) + "]"));
				cout<<"Shutting down connection for pool "<<name<<endl;
				network.CloseConnection(*goner->connection);
				routes.erase(goner);
			}
		}

		for(asizei loop = 0; loop < routes.size(); loop++) {
			auto r = std::find(toRead.cbegin(), toRead.cend(), routes[loop].connection);
			auto w = std::find(toWrite.cbegin(), toWrite.cend(), routes[loop].connection);
			bool canRead = r != toRead.cend();
			bool canWrite = w != toWrite.cend();
			if(!canRead && !canWrite) continue; // socket was not managed by me but that's fine.
			if(canRead || canWrite) {
				AbstractWorkSource &pool(*routes[loop].pool);
				auto happens(pool.Refresh(canRead, canWrite)); //!< \todo this was originally called at command completion... the fact bytes are sent does not mean the pool is right. Unsure which alternative is better
                if(happens.bytesReceived && onPoolCommand) onPoolCommand(pool);
                if(happens.diffChanged) diffChangeFunc(pool, pool.GetCurrentDiff());
                if(happens.newWork) dispatchFunc(pool, std::unique_ptr<stratum::AbstractWorkFactory>(pool.GenWork()));
			}
		}
	}

	const Network::ConnectedSocketInterface& GetConnection(const AbstractWorkSource &wsrc) const {
		for(asizei loop = 0; loop < routes.size(); loop++) {
			if(routes[loop].pool.get() == &wsrc) return *routes[loop].connection;
		}
		throw std::exception("Impossible, I manage everything!");
	}

	template<typename Func>
	void SetNoAuthorizedWorkerCallback(Func &&callback) {
		for(asizei loop = 0; loop < routes.size(); loop++) routes[loop].pool->SetNoAuthorizedWorkerCallback(callback);
	}

	asizei GetNumServers() const { return routes.size(); }
	AbstractWorkSource& GetServer(asizei pool) const { return *routes[pool].pool; }

	void AddPool(const PoolInfo &conf) {
		routes.push_back(Remote());
		ScopedFuncCall autopop([this]() { routes.pop_back(); });

		const char *host = conf.host.c_str();
		const char *port = conf.explicitPort.length()? conf.explicitPort.c_str() : conf.service.c_str();
		Network::ConnectedSocketInterface *conn = &network.BeginConnection(host, port);
		ScopedFuncCall clearConn([this, conn] { network.CloseConnection(*conn); });
		unique_ptr<FirstPoolWorkSource> stratum(new FirstPoolWorkSource("M8M/DEVEL", conf, *conn));
		stratum->errorCallback = [](asizei i, int errorCode, const std::string &message) {
			cout<<"Stratum message ["<<std::to_string(i)<<"] generated error response by server (code "
				<<std::dec<<errorCode<<"=0x"<<std::hex<<errorCode<<std::dec<<"), server says: \""<<message<<"\""<<endl;
		};
		routes.back().connection = conn;
		routes.back().pool = std::move(stratum);
		clearConn.Dont();
		autopop.Dont();
		addedPoolCount++;
	}

	asizei GetNumPoolsAdded() const { return addedPoolCount; }

private:
	Network &network;
	struct Remote {
		Network::ConnectedSocketInterface *connection;
		unique_ptr<AbstractWorkSource> pool;
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
	asizei addedPoolCount;
};
