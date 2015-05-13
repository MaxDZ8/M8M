/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "Network.h"


asizei NetworkInterface::AbstractDataSocket::Send(const aubyte *octects, asizei buffSize) {
	return Send(reinterpret_cast<const abyte*>(octects), buffSize);
}


asizei NetworkInterface::AbstractDataSocket::Receive(aubyte *storage, asizei buffSize) {
	return Receive(reinterpret_cast<abyte*>(storage), buffSize);
}


void WindowsNetwork::SetBlocking(SOCKET socket, bool blocks) {
	unsigned long state = !blocks; // 0 nonblocking disabled!
	if(ioctlsocket(socket, FIONBIO, &state)) throw std::exception("Cannot set socket blocking state.");
}


WindowsNetwork::WindowsNetwork() {
	WSADATA blah;
	if(WSAStartup(MAKEWORD(2, 2), &blah)) throw std::exception("Winsock2 failed to init.");
	
	if(!errMap.get()) {
		std::unique_ptr< std::map<int, SockErr> > temp(new std::map<int, SockErr>);
		auto add = [&temp](int soCode, SockErr portable) { temp->insert(std::make_pair(soCode, portable)); };

#if _WIN32
		add(0, se_OK);
		add(WSA_INVALID_HANDLE, se_badHandle);
		add(WSA_NOT_ENOUGH_MEMORY, se_outtaMemory);
		add(WSA_INVALID_PARAMETER, se_badParams);
		add(WSA_OPERATION_ABORTED, se_aborted);
		add(WSA_IO_INCOMPLETE, se_incomplete);
		add(WSA_IO_PENDING, se_willComplete);
		add(WSAEINTR, se_interrupted);
		add(WSAEBADF, se_badFile);
		add(WSAEACCES, se_denied);
		add(WSAEFAULT, se_badPointer);
		add(WSAEINVAL, se_badArg);
		add(WSAEMFILE, se_outtaFiles);
		add(WSAEWOULDBLOCK, se_wouldBlock);
		add(WSAEINPROGRESS, se_blockingOp);
		add(WSAEALREADY, se_alreadyPerformed);
		add(WSAENOTSOCK, se_notSocket);
		add(WSAEDESTADDRREQ, se_badDstAddress);
		add(WSAEMSGSIZE, se_tooLong);
		add(WSAEPROTOTYPE, se_badProtocol);
		add(WSAENOPROTOOPT, se_badProtocolOption);
		add(WSAEPROTONOSUPPORT, se_unsupportedProtocol);
		add(WSAESOCKTNOSUPPORT, se_unsupportedSocket);
		add(WSAEOPNOTSUPP, se_unsupportedOperation);
		add(WSAEPFNOSUPPORT, se_unsupportedFamily);
		add(WSAEAFNOSUPPORT, se_unsupportedAddress);
		add(WSAEADDRINUSE, se_usedPort);
		add(WSAEADDRNOTAVAIL, se_unavailable);
		add(WSAENETDOWN, se_netFail);
		add(WSAENETUNREACH, se_unreachable);
		add(WSAENETRESET, se_connReset);
		add(WSAECONNABORTED, se_connAborted);
		add(WSAECONNRESET, se_connAbortedRemotely);
		add(WSAENOBUFS, se_outtaBuffers);
		add(WSAEISCONN, se_alreadyConnected);
		add(WSAENOTCONN, se_notConnected);
		add(WSAESHUTDOWN, se_shutdown);
		add(WSAETOOMANYREFS, se_outtaReferences);
		add(WSAETIMEDOUT, se_timedOut);
		add(WSAECONNREFUSED, se_connRefused);
		add(WSAELOOP, se_cannotConvert);
		add(WSAENAMETOOLONG, se_nameTooLong);
		add(WSAEHOSTDOWN, se_remoteDown);
		add(WSAEHOSTUNREACH, se_unreachableHost);
		add(WSAENOTEMPTY, se_dirNotEmpty), // ??;
		add(WSAEPROCLIM, se_outtaProcesses);
		add(WSAEUSERS, se_outtaQuota);
		add(WSAEDQUOT, se_outtaStorageQuota);
		add(WSAESTALE, se_staleFile);
		add(WSAEREMOTE, se_remote);
		add(WSASYSNOTREADY, se_notReady);
		add(WSAVERNOTSUPPORTED, se_unsupportedImpVersion);
		add(WSANOTINITIALISED, se_notInitialized);
		add(WSAEDISCON, se_shuttingDown);
		add(WSAENOMORE, se_noMoreResults);
		add(WSAECANCELLED, se_callCancelled);
		add(WSAEINVALIDPROCTABLE, se_invalidProcTable);
		add(WSAEINVALIDPROVIDER, se_invalidProvider);
		add(WSAEPROVIDERFAILEDINIT, se_failedProviderInit);
		add(WSASYSCALLFAILURE, se_sysCallFailed);
		add(WSASERVICE_NOT_FOUND, se_badService);
		add(WSATYPE_NOT_FOUND, se_badClass);
		add(WSA_E_NO_MORE, se_noMoreResultsTWO);
		add(WSA_E_CANCELLED, se_cancelledCallE);
		add(WSAEREFUSED, se_refusedQuery);
		add(WSAHOST_NOT_FOUND, se_hostNotFound);
		add(WSATRY_AGAIN, se_tryAgain);
		add(WSANO_RECOVERY, se_noRecovery);
		add(WSANO_DATA, se_noDataRecord);
		add(WSA_QOS_RECEIVERS, se_qosRecvs);
		add(WSA_QOS_SENDERS, se_qosSends);
		add(WSA_QOS_NO_SENDERS, se_qosNoRecvs);
		add(WSA_QOS_NO_RECEIVERS, se_qosNoSends);
		add(WSA_QOS_REQUEST_CONFIRMED, se_qosReqConfirmed);
		add(WSA_QOS_ADMISSION_FAILURE, se_qosCommFail);
		add(WSA_QOS_POLICY_FAILURE, se_qosPolicyFail);
		add(WSA_QOS_BAD_STYLE, se_qosBadStyle);
		add(WSA_QOS_BAD_OBJECT, se_qosBadObject);
		add(WSA_QOS_TRAFFIC_CTRL_ERROR, se_qosBadTrafficControl);
		add(WSA_QOS_GENERIC_ERROR, se_qosGeneric);
		add(WSA_QOS_ESERVICETYPE, se_qosBadService);
		add(WSA_QOS_EFLOWSPEC, se_qosBadFlow);
		add(WSA_QOS_EPROVSPECBUF, se_qosBadProviderBuffer);
		add(WSA_QOS_EFILTERSTYLE, se_qosBadFilterStyle);
		add(WSA_QOS_EFILTERTYPE, se_qosBadFilter);
		add(WSA_QOS_EFILTERCOUNT, se_qosInvalidFilter);
		add(WSA_QOS_EOBJLENGTH, se_qosFilterCount);
		add(WSA_QOS_EFLOWCOUNT, se_qosFlowCount);
		add(WSA_QOS_EUNKOWNPSOBJ, se_qosBadObject);
		add(WSA_QOS_EPOLICYOBJ, se_qosBadPolicy);
		add(WSA_QOS_EFLOWDESC, se_qosBadFlowDesc);
		add(WSA_QOS_EPSFLOWSPEC, se_qosBadProviderFlowDesc);
		add(WSA_QOS_EPSFILTERSPEC, se_qosBadProviderFilterSpec);
		add(WSA_QOS_ESDMODEOBJ, se_qosBadDiscardMode);
		add(WSA_QOS_ESHAPERATEOBJ, se_qosBadShapeRate);
		add(WSA_QOS_RESERVED_PETYPE, se_qosPolicyReservedElement);
#endif


		errMap = std::move(temp);
	}
}


WindowsNetwork::~WindowsNetwork() {
	while(connecting.empty() == false) {
		auto el = connecting.begin();
		delete el->second;
		connecting.erase(el);
	}
	while(connections.empty() == false) {
		auto el = connections.begin();
		delete el->second;
		connections.erase(el);
	}
	while(servers.empty() == false) {
		auto el = servers.begin();
		delete el->second;
		servers.erase(el);
	}
	WSACleanup();
}


// Trailing return types are cool with better name resolution!
auto WindowsNetwork::BeginConnection(const char *host, const char *portService) -> std::pair<ConnectedSocketInterface*, ConnectionError> {
	std::unique_ptr<ConnectedSocket> newSocket(new ConnectedSocket(host, portService));
    ConnectedSocket *nullconn = nullptr;
	ConnectedSocketInterface *proxy = static_cast<ConnectedSocketInterface*>(newSocket.get());
	std::unique_ptr<PendingConnection> newConnection(new PendingConnection(newSocket.get()));
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	addrinfo *result = nullptr;
	ScopedFuncCall blast([result]() { if(result) freeaddrinfo(result); });
	if(getaddrinfo(host, portService, &hints, &result)) return std::make_pair(nullconn, ce_failedResolve);
	for(addrinfo *check = result; check; check = check->ai_next) {
		newConnection->candidates.reserve(newConnection->candidates.size() + 1);
		SOCKET up = socket(check->ai_family, check->ai_socktype, check->ai_protocol);
		if(up == INVALID_SOCKET) return std::make_pair(nullconn, ce_badSocket); // I assume this means I'm very low on resources
		SetBlocking(up, false);
		if(connect(up, check->ai_addr, int(check->ai_addrlen))) {
			auto error = GetSocketError();
			if(error == se_connRefused) continue; // this sometimes happens, for example connecting to "localhost" for some reason resolves to "0.0.0.0", which is invalid.
			if(GetSocketError() != se_wouldBlock) throw std::exception("Could not start connection procedure for socket.");
		}
		// I could just skip this but its' possibly indication of low resources, no point in even starting
		newConnection->candidates.push_back(up);
	}
	if(newConnection->candidates.size() == 0) return std::make_pair(nullconn, ce_noRoutes);
	PendingConnection *pending = newConnection.get();
	connecting.insert(std::make_pair(proxy, pending));
	ScopedFuncCall cancelPending([this, proxy]() { connecting.erase(proxy); });
	connections.insert(std::make_pair(proxy, newSocket.get()));
	cancelPending.Dont();
	newConnection.release();
	return std::make_pair(newSocket.release(), ce_ok);
}


bool WindowsNetwork::CloseConnection(ConnectedSocketInterface &object) {
	auto el = connections.find(&object);
	if(el == connections.cend()) return false;
	auto pending = connecting.find(&object);
	if(pending != connecting.cend()) {
		delete pending->second;
		connecting.erase(pending);
	}
	delete el->second;
	connections.erase(el);
	return true;
}


asizei WindowsNetwork::SleepOn(std::vector<SocketInterface*> &read, std::vector<SocketInterface*> &write, asizei timeoutms) {
	/* This is going to be more complicated than expected because this function really does two things.
	1- it transitions connecting sockets to connected
	2- it does a proper sleep. 
	I would just test the connecting sockets first and then the "ready" sockets, but this would imply a connection
	timeout could stop everything. I also cannot just put the object in the queue as there's no real socket object in the
	interface until it is connected. */
	fd_set readReady;
	fd_set writeReady;
	fd_set failures;
	FD_ZERO(&readReady);
	FD_ZERO(&writeReady);
	FD_ZERO(&failures);
	auto scan = [this, &failures](fd_set &set, std::vector<SocketInterface*> &monitor) -> SOCKET {
		SOCKET biggest = 0;
		for(asizei loop = 0; loop < monitor.size(); loop++) {
			auto el = connections.find(monitor[loop]);
			if(el == connections.cend()) { // maybe it's a listening socket
				auto servicing = servers.find(monitor[loop]);
				if(servicing == servers.cend()) { } // I am not managing this
				else {
					FD_SET(servicing->second->socket, &set);
					FD_SET(servicing->second->socket, &failures);
					biggest = max(biggest, servicing->second->socket);
				}
			}
			else if(el->second->socket != INVALID_SOCKET) { // managing this, and fully initialized
				FD_SET(el->second->socket, &set);
				FD_SET(el->second->socket, &failures);
				biggest = max(biggest, el->second->socket);
			}
			else { // still in the connecting state, then it can have a sequence of sockets.
				auto pending = connecting.find(monitor[loop]);
				if(pending == connecting.cend()) throw std::exception("Impossible, code incoherent (socket is connecting but no connecting state)!");
				auto &list(pending->second->candidates);
				for(asizei inner = 0; inner < list.size(); inner++) {
					FD_SET(list[inner], &set);
					FD_SET(list[inner], &failures);
					biggest = max(biggest, list[inner]);
				}
			}
		}
		return biggest;
	};
	SOCKET biggest = max(scan(readReady, read), scan(writeReady, write));
	biggest++; // select needs a +1 to test with strict inequality <
	timeval timeout;
	timeout.tv_sec = long(timeoutms / 1000);
	timeout.tv_usec = (timeoutms % 1000) * 1000;
	int result = select(int(biggest), &readReady, &writeReady, &failures, &timeout);
	if(!result) {
        for(auto &el : read) el = nullptr;
        for(auto &el : write) el = nullptr;
        return 0;
    }
	if(result < 1) throw std::exception("Some error occured while waiting for sockets to connect.");
	auto activated = [&failures, this](SocketInterface *hilevel, const fd_set &search) -> bool {
		auto el = connections.find(hilevel);
		if(el == connections.cend()) {
			auto listening = servers.find(hilevel);
			if(listening == servers.cend()) return false;
			bool ret = FD_ISSET(listening->second->socket, &failures) != 0;
			if(ret) listening->second->failed = true;
			return ret || FD_ISSET(listening->second->socket, &search) != 0;
		}
		auto pending = connecting.find(hilevel);
		if(pending == connecting.cend()) { // fully connected.
			bool ret = FD_ISSET(el->second->socket, &failures) != 0;
			if(ret) el->second->failed = true;
			return ret || FD_ISSET(el->second->socket, &search) != 0;
		}
		// if here, connection completed, move it to estabilished connections, but only if an active socket
		// is really found - we might have been awakened due to timeout or another socket!
		// We must also take care of the failing possibilities.
		for(asizei loop = 0; loop < pending->second->candidates.size(); loop++) {
			if(FD_ISSET(pending->second->candidates[loop], &failures)) {
				closesocket(pending->second->candidates[loop]);
                auto entry(pending->second->candidates.begin() + loop);
				pending->second->candidates.erase(entry);
                loop--;
			}
		}
		if(pending->second->candidates.empty()) { // will never finish connecting
			delete pending->second;
            connecting.erase(pending);
            el->second->failed = true;
			return true;
		}

		for(asizei loop = 0; loop < pending->second->candidates.size(); loop++) {
			if(FD_ISSET(pending->second->candidates[loop], &search)) {
				el->second->socket = pending->second->candidates[loop];
				delete pending->second;
				connecting.erase(pending);
				return true;
			}
		}
		return false;
	};
	asizei awaken = 0;
	for(asizei loop = 0; loop < read.size(); loop++) {
		if(activated(read[loop], readReady) == false)
			read[loop] = nullptr;
		else {
			awaken++;
			// a read handle awakens on connection close as well! Check it out by peeking at data.
			auto el = connections.find(read[loop]);
			if(el == connections.end()) continue;
			abyte byte;
			int res = recv(el->second->socket, &byte, sizeof(byte), MSG_PEEK);
			if(res <= 0) { // 0- connection closed gracefully, <0 aborted, see error
				auto err = GetSocketError();
				el->second->failed = true;
			}
		}
	}
	for(asizei loop = 0; loop < write.size(); loop++) {
		if(activated(write[loop], writeReady) == false)
			write[loop] = nullptr;
		else awaken++;
	}
	return awaken;
}


SockErr WindowsNetwork::GetSocketError() {
	auto soerr = WSAGetLastError();
	auto ret = errMap->find(soerr);
	if(ret == errMap->cend()) throw std::exception("WSAGetLastError returned unmapped error.");
	return ret->second;
}


asizei WindowsNetwork::ConnectedSocket::Send(const abyte *message, asizei count) {
	int len = count > 128 * 1024 * 1024? 128 * 1024 * 1024 : int(count); // max 128 MiB per write seems enough
	SOCKET sent = send(socket, message, len, 0);
	if(sent < 0) {
		this->failed = true;
		return 0;
	}
	return asizei(sent);
}


asizei WindowsNetwork::ConnectedSocket::Receive(abyte *storage, asizei count) {
	int len = count > 128 * 1024 * 1024? 128 * 1024 * 1024 : int(count); // max 128 MiB per write seems enough
	int received = recv(socket, storage, len, 0);
	if(received < 0) {
		int err = WSAGetLastError();
		if(err != WSAEWOULDBLOCK) failed = true; // throw std::exception("send(...) returned negative count, this is an unhandled error.");
		received = 0;
	}
	return asizei(received);
}


bool WindowsNetwork::ConnectedSocket::GotData() const {
	char dummy;
	int result = recv(socket, &dummy, sizeof(dummy), MSG_PEEK);
	return result > 0;
}


bool WindowsNetwork::ConnectedSocket::CanSend() const {
	fd_set me;
	FD_ZERO(&me);
	FD_SET(socket, &me);
	SOCKET result = select(int(socket + 1), NULL, &me, NULL, 0);
	return result != 0;
}


void WindowsNetwork::CloseServiceSocket(ServiceSocketInterface &what) {
	auto rem = servers.find(&what);
	if(rem != servers.end()) {
		delete rem->second;
		servers.erase(rem);
	}
}


NetworkInterface::ServiceSocketInterface& WindowsNetwork::NewServiceSocket(aushort port, aushort numPending) {
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;
	addrinfo *result = nullptr;
	ScopedFuncCall blast([result]() { if(result) freeaddrinfo(result); });
	char number[8]; // 64*1024+null
	if(port) _itoa_s(port, number, 10);
	if(getaddrinfo(NULL, port? number : NULL, &hints, &result)) throw std::exception("getaddrinfo failed.");
	SOCKET listener = INVALID_SOCKET;
	ScopedFuncCall clearSocket([&listener]() { if(listener != INVALID_SOCKET) closesocket(listener); });
	listener = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if(listener == INVALID_SOCKET) throw std::exception("Error creating new Service Socket, creation failed.");
    SetBlocking(listener, false);
	if(bind(listener, result->ai_addr, static_cast<int>(result->ai_addrlen)) == SOCKET_ERROR) throw std::exception("Error creating new Service Socket, could not bind.");
	if(listen(listener, numPending? numPending : SOMAXCONN) == SOCKET_ERROR) throw std::exception("Error creating new Service Socket, could enter listen state.");
	
	std::unique_ptr<ServiceSocket> add(new ServiceSocket(listener, port));
	clearSocket.Dont();
	servers.insert(std::make_pair(static_cast<SocketInterface*>(add.get()), add.get()));
	return *add.release();
}


NetworkInterface::ConnectedSocketInterface& WindowsNetwork::BeginConnection(ServiceSocketInterface &listener) {
	auto real(servers.find(&listener));
	if(real == servers.cend()) throw std::exception("Trying to create a connection from a socket not managed by this object.");
	SOCKET connSock = real->second->socket;
	SOCKET client = accept(connSock, NULL, NULL);
	if(client == INVALID_SOCKET) throw std::exception("Could not accept an incoming connection.");
	SetBlocking(client, false);
	ScopedFuncCall clearSocket([client]() { if(client) closesocket(client); });
	std::unique_ptr<ConnectedSocket> add(new ConnectedSocket(nullptr, nullptr));
	add->socket = client;
	clearSocket.Dont();
	connections.insert(std::make_pair(static_cast<SocketInterface*>(add.get()), add.get()));
	return *add.release();
}
