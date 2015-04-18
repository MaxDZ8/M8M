/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/StratumState.h"
#include <memory>
#include <map>
#include <time.h>
#include <rapidjson/document.h>
#include "../Common/AREN/ArenDataTypes.h"
#include "Stratum/Work.h"


using std::string;


/*! Objects implementing this interface are meant to be "schedulers" or policies
to decide which pool to use to when getting new work.
Legacy miners work by considering pool availability, distribute effort (time)
across pools or distribute results (shares). */
class AbstractWorkSource {
public:
    struct AlgoInfo {
        std::string name;
        bool bigEndian;
        aulong diffNumerator;
    };
    const AlgoInfo algo;
	const std::string name;
    const PoolInfo::MerkleMode merkleMode;
	
	std::function<void(const AbstractWorkSource &me, asizei id, StratumShareResponse shareStatus)> shareResponseCallback;

    struct Events {
        bool diffChanged = false;
        bool newWork = false;
        asizei bytesReceived = 0;
    };

	/*! Stratum pools are based on message passing. Every time we can, we must mangle input from the server,
	process it to produce output and act accordingly. The input task is the act of listening to the remote server
	to figure out if there's something to do. Pool servers will send a sequence of 1-line stratum commands and
	this basically takes the octects from the connection to an internal buffer to build a sequence of lines which
	are then dispatched to Mangle calls. This call processes the input and runs the Mangle calls.
	\param canRead true if the network layer detected bytes to be received without blocking.
	\param canWrite true if the network layer can dispatch bytes to be sent without blocking.
	\note Because Send and Receive are supposed to be nonblocking anyway parameters are somewhat redundant.
    \return .first is true if difficulty has been updated, .second is true if block data is updated. */
	Events Refresh(bool canRead, bool canWrite);

    /*! Returns a non-zero ntime value if the job is considered "current". If not, the job is considered "stale" and you might
    consider dropping the shares, or perhaps not. In practice, unless the outer code keeps some ntime around (for example by rolling)
    there's no chance those shares will be accepted as server needs a valid ntime to hash and 0 sure won't do it. */
    auint IsCurrentJob(const std::string &job) const { return stratum.GetJobNetworkTime(job); }

    //! Blindly send shares. Initially, this filtered the values by "stale" status but it's now just a forwarder, that logic is delegated.
    //! \return Index of this result in the global stream to the pool. You can use this to reconstruct info from callbacks.
	asizei SendShare(const std::string &job, auint ntime, auint nonce2, auint nonce) { 
		return stratum.SendWork(job, ntime, nonce2, nonce);
	}

	stratum::AbstractWorkFactory* GenWork() const;
    stratum::WorkDiff GetCurrentDiff() const { return stratum.GetCurrentDiff(); }

	//! Apparently stratum supports unsubscribing, but I cannot found the documentation right now.
	virtual void Shutdown() { }

	virtual ~AbstractWorkSource() { }

	bool NeedsToSend() const;

	PoolInfo::DiffMultipliers GetDiffMultipliers() const;
	void GetUserNames(std::vector< std::pair<const char*, StratumState::AuthStatus> > &list) const;

	template<typename Func>
	void SetNoAuthorizedWorkerCallback(Func &&callback) { stratum.allWorkersFailedAuthCallback = callback; }

	asizei GetNumUsers() const { return stratum.GetNumWorkers(); }
	StratumState::WorkerNonceStats GetUserShareStats(asizei ui) const { return stratum.GetWorkerStats(ui); }

protected:
	/*! Used to enumerate workers to register to this remote server.
	Return nullptr as first element to terminate enumeration. */
	typedef std::vector< std::pair<const char*, const char*> > Credentials;
	AbstractWorkSource(const char *presentation, const char *name, const AlgoInfo &algo, std::pair<PoolInfo::DiffMode, PoolInfo::DiffMultipliers> diffDesc, PoolInfo::MerkleMode mm, const Credentials &v);

	virtual void MangleReplyFromServer(size_t id, const rapidjson::Value &result, const rapidjson::Value &error) = 0;

	/*! The original stratum implementation was fairly clear on what was a notification (no need to confirm <-> no id)
	and what was a request (confirmed by id). Author of P2Pool decided it would have been a nice idea to attach ids to everything,
	so I cannot tell the difference anymore. To be called with non-null signature. */
	virtual void MangleMessageFromServer(const std::string &idstr, const char *signature, const rapidjson::Value &notification) = 0;

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
	// Iterating on nonces is fully miner's responsability now. We only tell it if it can go on or not.
	//auint nonce2;

    class BuildingWorkFactory : public stratum::AbstractWorkFactory {
    public:
        BuildingWorkFactory(bool restartWork, auint networkTime, const CBHashFunc cbmode, const std::string &poolJob)
            : AbstractWorkFactory(restartWork, networkTime, cbmode, poolJob) { }
        void SetBlankHeader(const std::array<aubyte, 128> &blank, bool littleEndianAlgo, adouble algoDiffNumerator) {
            blankHeader = blank;
            ExtractNetworkDiff(littleEndianAlgo, algoDiffNumerator);
        }
        void SetCoinbase(std::vector<aubyte> &binary, asizei n2off) {
            coinbase = std::move(binary);
            nonceTwoOff = n2off;
        }
        void SetMerkles(const std::vector<btc::MerkleRoot> &merkles, asizei offset) {
            merkleOff = offset;
            this->merkles.resize(merkles.size());
            for(asizei cp = 0; cp < merkles.size(); cp++) this->merkles[cp] = merkles[cp].hash;
        }
        adouble GetNetworkDiff() const { return networkDiff; }

    private:
        adouble networkDiff;
	    void ExtractNetworkDiff(bool littleEndianAlgo, aulong algoDiffNumerator) {
		    // This looks like share difficulty calculation... but not quite.
		    aulong diff;
		    double numerator;
		    const auint *src = reinterpret_cast<const auint*>(blankHeader.data() + 72);
		    if(littleEndianAlgo) {
			    aulong blob = BETOH(*src) & 0xFFFFFF00;
			    blob <<= 8;
			    blob = 
			    diff = SWAP_BYTES(blob);
			    numerator = static_cast<double>(algoDiffNumerator);
		    }
		    else {
			    aubyte pow = blankHeader[72];
			    aint powDiff = (8 * (0x1d - 3)) - (8 * (pow - 3)); // don't ask.
			    diff = BETOH(*src) & 0x0000000000FFFFFF;
			    numerator = static_cast<double>(algoDiffNumerator << powDiff);
		    }
		    if(diff == 0) diff = 1;
		    networkDiff = numerator / double(diff);
	    }
    };
};
