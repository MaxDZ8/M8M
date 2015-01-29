/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AREN/ArenDataTypes.h"
#include <vector>
#include "ScopedFuncCall.h"
#include <memory>
#include <map>
#include <set>

#if defined(_WIN32)
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#endif


enum SockErr {
	se_OK,
	se_badHandle,
	se_outtaMemory,
	se_badParams,
	se_aborted,
	se_incomplete,
	se_willComplete,
	se_interrupted,
	se_badFile,
	se_denied,
	se_badPointer,
	se_badArg,
	se_outtaFiles,
	se_wouldBlock,
	se_blockingOp,
	se_alreadyPerformed,
	se_notSocket,
	se_badDstAddress,
	se_tooLong,
	se_badProtocol,
	se_badProtocolOption,
	se_unsupportedProtocol,
	se_unsupportedSocket,
	se_unsupportedOperation,
	se_unsupportedFamily,
	se_unsupportedAddress,
	se_usedPort,
	se_unavailable,
	se_netFail,
	se_unreachable,
	se_connReset,
	se_connAborted,
	se_connAbortedRemotely,
	se_outtaBuffers,
	se_alreadyConnected,
	se_notConnected,
	se_shutdown,
	se_outtaReferences,
	se_timedOut,
	se_connRefused,
	se_cannotConvert,
	se_nameTooLong,
	se_remoteDown,
	se_unreachableHost,
	se_dirNotEmpty, // ?
	se_outtaProcesses,
	se_outtaQuota,
	se_outtaStorageQuota,
	se_staleFile,
	se_remote,
	se_notReady,
	se_unsupportedImpVersion,
	se_notInitialized,
	se_shuttingDown,
	se_noMoreResults,
	se_callCancelled,
	se_invalidProcTable,
	se_invalidProvider,
	se_failedProviderInit,
	se_sysCallFailed,
	se_badService,
	se_badClass,
	se_noMoreResultsTWO,
	se_cancelledCallE,
	se_refusedQuery,
	se_hostNotFound,
	se_tryAgain,
	se_noRecovery,
	se_noDataRecord,
	se_qosRecvs,
	se_qosSends,
	se_qosNoRecvs,
	se_qosNoSends,
	se_qosReqConfirmed,
	se_qosCommFail,
	se_qosPolicyFail,
	se_qosBadStyle,
	se_qosBadObject,
	se_qosBadTrafficControl,
	se_qosGeneric,
	se_qosBadService,
	se_qosBadFlow,
	se_qosBadProviderBuffer,
	se_qosBadFilterStyle,
	se_qosBadFilter,
	se_qosInvalidFilter,
	se_qosFilterCount,
	se_qosFlowCount,
	se_qosUnrecognizedObject,
	se_qosBadPolicy,
	se_qosBadFlowDesc,
	se_qosBadProviderFlowDesc,
	se_qosBadProviderFilterSpec,
	se_qosBadDiscardMode,
	se_qosBadShapeRate,
	se_qosPolicyReservedElement,
};


/*! Those objects are meant to provide abstractions over socket creation, destruction
and management. It is better to have a single pool of sockets as each object only 
operates on its own objects, but there might be good reasons to allow multiple instances
(for example, to give each thread its own socket manager) so this is left to the
outer code. */
class NetworkInterface {
public:
	static size_t connectionTimeoutSeconds;

	class SocketInterface {
	public:
		virtual bool Works() const = 0; //!< false if an error occured
		virtual ~SocketInterface() { }
	};

	class AbstractDataSocket : public SocketInterface {
	public:
		virtual asizei Send(const abyte *octects, asizei count) = 0;
		virtual asizei Receive(abyte  *octects, asizei buffSize) = 0;
		/*! \return Number of bytes sent. If this is zero this might be signaling an error, and you should check if this
		socket still Works(). This is due to the fact you are encouraged (but not forced) to use sockets in nonblocking mode
		so this should always be able to consume at least an octet. */
		asizei Send(const aubyte *octects, asizei count);
		asizei Receive(aubyte  *octects, asizei buffSize);
		virtual bool GotData() const = 0;
		virtual bool CanSend() const = 0;
		virtual bool Works() const = 0; //!< false if an error occured
	};
	class ConnectedSocketInterface : public AbstractDataSocket {
	public:
		virtual std::string PeerHost() const = 0;
		virtual std::string PeerPort() const = 0;
	};

	/*! Some sockets have the only purpose of accepting incoming connections. Those sockets are modelled using an objectof this type. 
	They are only allowed to be used for either SleepOn or Accept. */
	class ServiceSocketInterface : public SocketInterface {
	public:
		/*! The port address to be used by clients to connect to this socket/service.
		It is especially useful to get back the port when automatically assigned (but when do you want to do that?) */
		virtual aushort GetPort() const = 0;
		virtual ~ServiceSocketInterface() { }
	};

	virtual ~NetworkInterface() { }

	/*! Notice this function is called BeginConnection, not connect or something.
	It immediately returns a socket so you can start building objects on top of it but it's very likely the connection
	procedures will not be completed by the time this returns. It is necessary to use SleepOn putting this in the receiver
	queue to figure out when the connection is ready to go - this will also allow the object to transition to a fully
	working state without requiring thread locks. 
	In other terms, the returned connection is technically a future, but the object isn't so it is returned immediately. */
	virtual ConnectedSocketInterface& BeginConnection(const char *host, const char *portService) = 0;

	//! If false is returned, then the object passed was not managed by this, and call is NOP.
	virtual bool CloseConnection(ConnectedSocketInterface &object) = 0;

	/*! This function halts the program until at least one of the following conditions occur:
	1- at least 1 socket in the read list has received at least 1 byte of data;
	  1a- Special for service sockets: those don't receiva data but rather connection requests to Accept.
	2- at least 1 socket in the write list has buffer space to allow instant non-blocking send;
	3- at least one socket completes connection, regardless you placed it in write or read list;
	3- timeout is exceeded;
	4- an error in monitored sockets (either send or receive list) is detected.
	In the last case, the function will throw.
	If timeout is exceeded, return value is zero.
	In other cases, the return value is not-zero but due to issues in managing connecting sockets,
    you should not assume this specific value has a meaning.
	The pointers in the vectors passed do maintain their position but not their value.
	The value is kept only for pointers which caused wakeup. Sleeping pointers are cleared.
	This function is also used to perform full socket initialization in the case of connected streams. 
	There's no guarantee successive reads or writes to consume 1 byte or more, but only they'll be instant.
	Sockets not managed by this are ignored.
	\note If a socket fails while sleeping, it will still provide an activation so outer code can react.
	It is therefore strongly suggested to always check socket's Works(). Using CanSend() and GotData()
	would be a bit overkill and might actually not work. */
	virtual asizei SleepOn(std::vector<SocketInterface*> &read,
	                       std::vector<SocketInterface*> &write, asizei timeoutms) = 0;
	virtual SockErr GetSocketError() = 0;

	/*! Creates a "service socket" on the local machine. It's a special "listen" socket used by clients to estabilish
	new connections to this machine.
	\param port number of local port to use on this machine. If 0, assigned by system.
	\param numPending amount of maximum connections supported by this socket. If 0, maximum supported by the system. */
	virtual ServiceSocketInterface& NewServiceSocket(aushort port, aushort numPending) = 0;

	virtual void CloseServiceSocket(ServiceSocketInterface &what) = 0;
	
	/*! Creates a connection by pulling out a connection request from a service socket. Always call this AFTER SleepOn
	has returned indicating availability of a connection request to handle; other conditions are considered errors. */
	virtual ConnectedSocketInterface& BeginConnection(ServiceSocketInterface &listener) = 0;
};


class WindowsNetwork : public NetworkInterface {
private:
	class ConnectedSocket : public ConnectedSocketInterface { 
	public:
		const std::string host;
		const std::string port;
		SOCKET socket;
		bool failed;
		ConnectedSocket(const char *hostname, const char *portOrService)
			: host(hostname? hostname : ""), port(portOrService? portOrService : ""), socket(INVALID_SOCKET), failed(false) {
		}
		~ConnectedSocket() { if(socket != INVALID_SOCKET) closesocket(socket); }
		std::string PeerHost() const { return host; }
		std::string PeerPort() const { return port; }
		asizei Send(const abyte *octects, asizei count);
		asizei Receive(abyte  *octects, asizei buffSize);
		bool GotData() const;
		bool CanSend() const;
		bool Works() const { return !failed; }
	};

	struct ServiceSocket : public ServiceSocketInterface {
		const aushort port;
		SOCKET socket;
		bool failed;
		ServiceSocket(SOCKET s, aushort p) : socket(s), port(p), failed(false) { }
		~ServiceSocket() { if(socket != INVALID_SOCKET) closesocket(socket); }
		aushort GetPort() const { return port; }
		bool Works() const { return !failed; }
	};

	void SetBlocking(SOCKET socket, bool blocks);
	
	//! Maps Win32 error codes to my portable codes.
	static std::unique_ptr< std::map<int, SockErr> > errMap;


	struct PendingConnection {
		ConnectedSocket *which;
		std::vector<SOCKET> candidates;
		PendingConnection(ConnectedSocket *resource = nullptr) : which(resource) { }
		~PendingConnection() {
			for(asizei loop = 0; loop < candidates.size(); loop++) {
				if(which->socket == candidates[loop] || candidates[loop] == INVALID_SOCKET) continue;
				//! \todo maybe we should call shutdown() to ensure all queued data makes it to the server first
				closesocket(candidates[loop]);
			}
		}
		bool operator==(const ConnectedSocket *s) const { return which == s; }
	};

	//! This could also go for unique_ptr, but it makes iterators quite more ugly so...
	//! This is a map, and not a set, so I can also avoid using dynamic_cast
	std::map<SocketInterface*, ConnectedSocket*> connections;

	//! This could really use unique_ptr, but I've decided to not use it there as the management is quite compact
	std::map<SocketInterface*, PendingConnection*> connecting;

	std::map<SocketInterface*, ServiceSocket*> servers;

public:
	WindowsNetwork();
	~WindowsNetwork();
	ConnectedSocketInterface& BeginConnection(const char *host, const char *portService);
	bool CloseConnection(ConnectedSocketInterface &object);

	asizei SleepOn(std::vector<SocketInterface*> &read, std::vector<SocketInterface*> &write, asizei timeoutms);
	SockErr GetSocketError();
	
	ServiceSocketInterface& NewServiceSocket(aushort port, aushort numPending);
	void CloseServiceSocket(ServiceSocketInterface &what);
	ConnectedSocketInterface& BeginConnection(ServiceSocketInterface &listener);
};


typedef WindowsNetwork Network;
