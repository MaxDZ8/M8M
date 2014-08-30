/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/Stratum/AbstractWorkUnit.h"
#include "../Common/StratumState.h"
#include <memory>
#include <map>
#include <time.h>
#include <json/json.h>
#include "../Common/AREN/ArenDataTypes.h"


// #define M8M_STRATUM_DUMPTRAFFIC


#if defined(M8M_STRATUM_DUMPTRAFFIC)
#include <fstream>
#endif

using std::string;


/*! Objects implementing this interface are meant to be "schedulers" or policies
to decide which pool to use to when getting new work.
Legacy miners work by considering pool availability, distribute effort (time)
across pools or distribute results (shares). */
class AbstractWorkSource {
public:
	const std::string algo;
	const std::string name;
	enum Event {
		e_nop,
		e_gotRemoteInput,
		e_newWork
	};
	
	std::function<void(bool success)> shareResponseCallback;

	/*! Stratum pools are based on message passing. Every time we can, we must mangle input from the server,
	process it to produce output and act accordingly. The input task is the act of listening to the remote server
	to figure out if there's something to do. Pool servers will send a sequence of 1-line stratum commands and
	this basically takes the octects from the connection to an internal buffer to build a sequence of lines which
	are then dispatched to Mangle calls. This call processes the input and runs the Mangle calls.
	\param canRead true if the network layer detected bytes to be received without blocking.
	\param canWrite true if the network layer can dispatch bytes to be sent without blocking.
	\note Because Send and Receive are supposed to be nonblocking anyway parameters are somewhat redundant.*/
	virtual Event Refresh(bool canRead, bool canWrite);

	bool SendShares(const std::string &job, auint nonce2, const std::vector<auint> &nonces) { 
		auint ntime = stratum.GetJobNetworkTime(job);
		if(!ntime) return false;
		for(asizei loop = 0; loop < nonces.size(); loop++) stratum.SendWork(ntime, nonce2, nonces[loop]);
		return true;
	}

	stratum::AbstractWorkUnit* GenWorkUnit() { return stratum.GenWorkUnit(); }

	//! Apparently stratum supports unsubscribing, but I cannot found the documentation right now.
	virtual void Shutdown() { }

	virtual ~AbstractWorkSource() { }

	bool NeedsToSend() const;

	//! This call is to allow outer code to understand which pool is being manipulated.
	const char* GetName() const { return name.c_str(); }

	aulong GetCoinDiffMul() const;
	void GetUserNames(std::vector< std::pair<const char*, StratumState::AuthStatus> > &list) const;

	template<typename Func>
	void SetNoAuthorizedWorkerCallback(Func &&callback) { stratum.allWorkersFailedAuthCallback = callback; }

	asizei GetNumUsers() const { return stratum.GetNumWorkers(); }
	StratumState::WorkerNonceStats GetUserShareStats(asizei ui) const { return stratum.GetWorkerStats(ui); }

protected:
	/*! Used to enumerate workers to register to this remote server.
	Return nullptr as first element to terminate enumeration. */
	typedef std::vector< std::pair<const char*, const char*> > Credentials;
	AbstractWorkSource(const char *presentation, const char *name, const char *algo, aulong coinDiffMul, PoolInfo::MerkleMode mm, const Credentials &v);

	virtual void MangleReplyFromServer(size_t id, const Json::Value &result, const Json::Value &error) = 0;

	/*! The original stratum implementation was fairly clear on what was a notification (no need to confirm <-> no id)
	and what was a request (confirmed by id). Author of P2Pool decided it would have been a nice idea to attach ids to everything,
	so I cannot tell the difference anymore. To be called with non-null signature. */
	virtual void MangleMessageFromServer(const std::string &idstr, const char *signature, const Json::Value &notification) = 0;

	/*! Sending and receiving data is left to a derived class. This call tries to send stratum blobs.
	The send must be implemented in a non-blocking way, if no bytes can be sent right away, it can return 0
	as the number of bytes sent. The current implementation calls this once per tick, at the beginning of Refresh. */
	virtual asizei Send(const abyte *data, const asizei count) = 0;

	/*! Again, this must NOT BLOCK. If no data can be returned immediately, return 0 bytes. */
	virtual asizei Receive(abyte *storage, asizei rem) = 0;

	StratumState stratum;

private:	
	/*! Data received by calling Receive(...) is stored here. Then, a pass searches for
	newline messages and dispatches them to parsers. */
	struct RecvBuffer {
		std::unique_ptr<char[]> data;
		asizei allocated;
		asizei used;
		char* NextBytes() const { return data.get() + used; }
		asizei Remaining() const { return allocated - used; }
		bool Full() const { return used == allocated; }
		void Grow() {
			char *bigger = new char[allocated + 2048];
			memcpy_s(bigger, allocated + 2048, data.get(), used);
			data.reset(bigger);
			allocated += 2048;
		}
		RecvBuffer() : used(0), data(new char[4096]), allocated(4096)  { 
			#if _DEBUG
				memset(data.get(), 0, 4096);
			#endif
		}
		// question is: why not to just use a std::vector, which has a data() call anyway?
		// Because I really want to be sure of the allocation/deallocation semantics.
		// Is  it worth it? Probably not.
	} recvBuffer;
	Json::Reader json;
	// Iterating on nonces is fully miner's responsability now. We only tell it if it can go on or not.
	//auint nonce2;

#if defined(M8M_STRATUM_DUMPTRAFFIC)
	std::ofstream stratumDump;
#endif
};
