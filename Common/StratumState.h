/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <memory>
#include <queue>
#include <map>
#include <time.h>
#include "../Common/Stratum/messages.h"
#include "../Common/Stratum/parsing.h" // DecodeHEX
#include "../Common/AREN/ScopedFuncCall.h"
#include "../Common/AREN/SerializationBuffers.h"
#include "../Common/BTC/Funcs.h"
#include <sstream>
#include <iomanip>
#include "PoolInfo.h"
#include "Stratum/Work.h"

using std::string;


enum StratumShareResponse {
    ssr_accepted,
    ssr_rejected,
    ssr_expired     //!< Server did not reply in a long time and we're dropping the entry. Do whatever you want with this.
};

/*! Stratum is a stateful protocol and this structure encapsulates it.
Legacy miners put everything in a single structure regarding pools.
So do I, but I do it with some care. */
class StratumState {
protected:
	template<typename ValueType>
	string KeyValue(const char *key, const ValueType &value, bool quoteValue = true) {
		std::stringstream str;
		str<<'"'<<key<<'"'<<": ";
		if(quoteValue)  str<<'"'<<value<<'"';
		else str<<value;
		return str.str();
	}
	template<typename ValueType>
	string Quoted(const ValueType &str) {
		string uff("\"");
		return uff + str + "\"";
	}

public:
	std::function<void(asizei id, StratumShareResponse shareStatus)> shareResponseCallback;
	std::function<void()> allWorkersFailedAuthCallback;
	aulong errorCount;

	const PoolInfo::DiffMultipliers diffMul;
	PoolInfo::DiffMode diffMode;
	StratumState(const char *presentation, const PoolInfo::DiffMultipliers &diffMul);
	const string& GetSessionID() const { return subscription.sessionID; }

	/*! The outer code should maintain a copy of this value. This to understand when the
	work unit sent by the remote host has changed. It then makes sense to call RestartWork(). */
	time_t LastNotifyTimestamp() const { return dataTimestamp; }

    /*! Returns true if the connection has been estabilished, in the sense of mining.subscribe received a response.
    This does not mean work can be carried out: you need to authorize workers first... but at least it can be generated! */
    bool Subscribed() const { return subscription.sessionID.empty() == false; }

    //! \sa void Notify(const stratum::MiningSetDifficultyNotify&)
    stratum::WorkDiff GetCurrentDiff() const;

    //! \sa void Notify(const stratum::MiningSetDifficultyNotify&)
    stratum::MiningNotify GetCurrentJob() const { return block; }

    stratum::MiningSubscribeResponse GetSubscription() const { return subscription; }

	/*! Registering a worker (server side concept) is something which must be performed by outer code.
	It is best to do that after mining.notify but before generating a work unit.
	This effectively queues "mining.authorize" messages to server. Keep in mind it will take some time
	before workers are authorized. Workers are never registered more than once (function becomes NOP).
	\param user name to use to authorize worker (server-side concept).
	\param psw the password can be empty string. */
	void Authorize(const char *user, const char *psw);

	/*! Returns true if the given worker has attempted authorization. It might be still being authorized
	or perhaps it might have failed registration. \sa CanSendWork */
	bool IsWorker(const char *name) const;

	/*! If the job is still considered active then this returns the job ntime to be used for share submission.
	Otherwise, it returns 0. That is considered by the stratum manager too old and thus likely to be rejected.
	The outer code shall drop those shares as they have no valid ntime to be produced. */
	auint GetJobNetworkTime(const std::string &job) const { return block.job == job? block.ntime : 0; }

	enum AuthStatus {
		as_pending,
		as_accepted,
		as_inferred,	//!< Used if the server does not reply to authorization but accepts shares. This was really a misunderstanding by my side but since it's there, I leave it.
		as_notRequired, //!< the case for p2pool, but I just consider that an implicit as_accepted
		as_failed
	};
	AuthStatus GetAuthenticationStatus(const char *name) const;

    //! Returns unique number identifying the nonce sent to reconstruct info by other code on callbacks.
	asizei SendWork(const std::string &job, auint ntime, auint nonce2, auint nonce);
	
	/*! This structure contains an indivisible amount of information to	send the server via socket.
	The message might be long, and multiple calls to send(...) might be needed. Rather than blocking
	this thread I keep state about the messages and I send them in order.
	Scheduling is easier and performed at higher level. */
	struct Blob {
		__int8 *data;
		const size_t total;
		const size_t id;
		size_t sent;
		Blob(const __int8 *msg, size_t count, size_t msgID)
			: total(count), sent(0), data(nullptr), id(msgID) {
			data = new __int8[total];
			memcpy_s(data, total, msg, total);
		}
		// note no destructor --> leak. I destruct those when the pool is destroyed so copy
		// is easy and no need for unique_ptr
	};
	std::queue<Blob> pending;

	~StratumState() {
		while(pending.size()) {
			Blob &blob(pending.front());
			delete[] pending.front().data;
			pending.pop();
		}
	}

	// .id and .method --> Request \sa RequestReplyReceived
	void Request(const stratum::ClientGetVersionRequest &msg);

	// .id and .result --> Response (to some of our previous inquiry)
	/*! Call this function to understand what kind of request resulted in the
	received response. Use this to select a proper parsing methodology. */
	const char* Response(size_t id) const;

	void Response(size_t id, const stratum::MiningSubscribeResponse &msg) { subscription = msg; }
	void Response(asizei id, const stratum::MiningAuthorizeResponse &msg);
	void Response(asizei id, const stratum::MiningSubmitResponse &msg);
	
	void Notify(const stratum::MiningSetDifficultyNotify &msg) { difficulty = msg.newDiff; }
	void Notify(const stratum::MiningNotify &msg);

	/*! When a reply to a request is received, no matter if successful or not, you use Response to identify the original type of request and then issue
	a Response call accordingly. After processing has elapsed, call this function the free the resources allocated to remember the message which was just processed.
	Note to this purpose the success flag is not dependant on the message itself but whathever it was a response rather than an error.
	\param[in] id To identify the message to be removed.
	\param[in] success Set this to false if the reply was an error.
	\note Signaling an error might trigger additional actions. */
	void RequestReplyReceived(asizei id, bool error);

	// Not very useful stuff: pull out worker data. Useful for monitoring purposes.
	asizei GetNumWorkers() const { return workers.size(); }
	std::pair<const char*, AuthStatus> GetWorkerInfo(const asizei i) const {
		const Worker &w(workers[i]);
		return std::make_pair(w.name.c_str(), GetAuthenticationStatus(w.name.c_str())); // blergh
	}
	
	struct WorkerNonceStats {
		asizei sent;
		asizei accepted, rejected;
		WorkerNonceStats() : sent(0), accepted(0), rejected(0) { }
		bool operator!=(const WorkerNonceStats &other) const { return sent != other.sent || accepted != other.accepted || rejected != other.rejected; }
	};
	WorkerNonceStats GetWorkerStats(const asizei i) const { return workers[i].nonces; }

private:
	size_t nextRequestID;
	time_t dataTimestamp;
	stratum::MiningSubscribeResponse subscription; //!< comes handy!
	stratum::MiningNotify block; //!< sent by remote server and stored here
	double difficulty;
    std::string nameVer;
	
	struct Worker {
		std::string name; //!< server side login credentials
		const asizei id; //!< message ID used to request authorization. Not const because I build those after putting the message in queue.
		time_t authorized; //!< time when the authorization result was received, implies validity of canWork flag.
		AuthStatus authStatus; //!< only meaningful after this.authorization
		WorkerNonceStats nonces;
		Worker(const char *login, const asizei msgIndex) : id(msgIndex), authorized(0), authStatus(as_pending), name(login) { }
	};
	std::vector<Worker> workers;

	//! Messages generated by the client must have their ids generated.
	//!\returns the used ID.
	asizei PushMethod(const char *method, const string &pairs);

	//! When replying to the server instead we have to use its id, which can be arbitrary.
	void PushResponse(const std::string &id, const string &pairs);

	/*! The requests I send to the server have an ID I decide.  The server will reply with that ID and
	I'll have to remember what request I made to it. This maps IDs to request strings so I can support
	help the outer code in mangling responses. */
	std::map<size_t, const char*> pendingRequests;

	/*! Maps a mining.submit to the worker originating it so I can keep count of accepted/rejected shares. */
	std::map<size_t, Worker*> submittedWork;


    /*! Build the "target string", a more accurate representation of the real 256-bit difficulty.
    In the beginning this was a LTC miner and had an hardcoded trueDiffOne multiplier of 64*1024, as LTC has.
    Now this is no more the case and the trueDiffOne multiplier must be passed from outer decisions. It does not really affect computation but
    it's part of the stratum state as it's a function of coin being mined.
    \note This was originally in the BTC namespace but it seems the BTC protocol does not care about "difficulty". It is mostly a representation
    thing and often used with stratum (for some reason) so it has been moved there, which is the only place it's used. */
    static std::array<aulong, 4> MakeTargetBits_BTC(adouble diff, adouble diffOneMul);

    /*! A slightly modified / simplified form of target calculation, premiered with NeoScrypt.
    It is, at its core, more or less the same thing: dividing by power-of-two so it keeps being precise on progressive divisions. */
    static std::array<aulong, 4> MakeTargetBits_NeoScrypt(adouble diff, adouble diffOneMul);
};

/*! \todo It appears obvious this object is not in the correct place
it should be in the stratum namespace I guess
and likely also have a different name
Still here until I figure out the details and separation of concerns. */
