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
#include "PoolInfo.h"


/*! After more than 1 year in the works, I think it's a good idea to recap what AbstractWorkSources were supposed to be and what they really are.
In the initial design they were supposed to be hubs to groups of pools, with derived classes implementing the policy to decide which pool to use to when getting new work.
Those hubs were supposed to implement the functionality legacy miners use to distribute work across multiple pools.

In practice, the only thing I managed to envision was the "WorkSource", according to the idea of derived objects to be set of pools, with this being the simplest
policy of dropping everything but the first pool.
This wasn't the case even a few days after WorkSource was deployed. The real deal with those objects (as used in code) has always been:
1- they represent a remote server
2- they are persistent objects
or, in other terms, they represent a pool as described in the config file (and not necessarily connected/working).

The initial formulation did not allow that. The only way to reboot a connection would be to create a new object. Yeah, in theory I could do
manual dtor calling and placement new but this would just be hiding the problem. The problem also has a simple solution: just have a call to reset internal state.
It's easy as this has fairly contained state: it's either the recv buffer or the stratum state. */
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
    const PoolInfo::DiffMultipliers diffMul;
    const PoolInfo::DiffMode diffMode;

	std::function<void(const AbstractWorkSource &me, asizei id, StratumShareResponse shareStatus)> shareResponseCallback;
    std::function<void(const AbstractWorkSource &, const std::string &worker, StratumState::AuthStatus status)> workerAuthCallback;

    struct Events {
        bool diffChanged = false;
        bool newWork = false;
        asizei bytesReceived = 0;
        bool connFailed = false;
    };

	/*! Stratum pools are based on message passing. Every time we can, we must mangle input from the server,
	process it to produce output and act accordingly. The input task is the act of listening to the remote server
	to figure out if there's something to do. Pool servers will send a sequence of 1-line stratum commands and
	this basically takes the octects from the connection to an internal buffer to build a sequence of lines which
	are then dispatched to Mangle calls. This call processes the input and runs the Mangle calls.
	\param canRead true if the network layer detected bytes to be received without blocking.
	\param canWrite true if the network layer can dispatch bytes to be sent without blocking.
	\note Because Send and Receive are supposed to be nonblocking anyway parameters are somewhat redundant. */
	Events Refresh(bool canRead, bool canWrite);

    /*! Returns a non-zero ntime value if the job is considered "current". If not, the job is considered "stale" and you might
    consider dropping the shares, or perhaps not. In practice, unless the outer code keeps some ntime around (for example by rolling)
    there's no chance those shares will be accepted as server needs a valid ntime to hash and 0 sure won't do it.
    \note Maybe the connection's gone belly up and we'll just consider everything stale. */
    auint IsCurrentJob(const std::string &job) const { return stratum? stratum->GetJobNetworkTime(job) : 0; }

    /*! Blindly send shares. Initially, this filtered the values by "stale" status but it's now just a forwarder, that logic is delegated.
    \return Index of this result in the global stream to the pool. You can use this to reconstruct info from callbacks.
    \note It is assumed this is called only if IsCurrentJob returns true so stratum is assumed to be there. */
	asizei SendShare(const std::string &job, auint ntime, auint nonce2, auint nonce) {
		return stratum->SendWork(job, ntime, nonce2, nonce);
	}

    /*! The usage pattern involves calling this only if Refresh returns Events::newWork = true, which happens only if a valid stratum object exists.
    Just polling at random will cause miners to reboot nonce count which will give you rejects.
    The nullptr factory is a valid non-factory which means "no work to do". Create this yourself on need. */
	stratum::AbstractWorkFactory* GenWork() const;
    stratum::WorkDiff GetCurrentDiff() const;

	//! Apparently stratum supports unsubscribing, but I cannot found the documentation right now.
	virtual void Shutdown() { ClearStratum(); }

	virtual ~AbstractWorkSource() { }

	bool NeedsToSend() const;

	void GetUserNames(std::vector< std::pair<const char*, StratumState::AuthStatus> > &list) const;

    void RefreshCallbacks() { if(stratum) SetStratumCallbacks(); }

    bool Ready() const { return stratum != nullptr; } //!< A pool is ready if it is going to produce work. Not necessarily already producing or accepting.

    //! Contains a static array with difficulty numerator and endianess info for all known algos.
    static AlgoInfo GetCanonicalAlgoInfo(const char *algo);

protected:
	AbstractWorkSource(const char *name, const AlgoInfo &algo, std::pair<PoolInfo::DiffMode, PoolInfo::DiffMultipliers> diffDesc, PoolInfo::MerkleMode mm);

	virtual void MangleReplyFromServer(size_t id, const rapidjson::Value &result, const rapidjson::Value &error) = 0;

	/*! The original stratum implementation was fairly clear on what was a notification (no need to confirm <-> no id)
	and what was a request (confirmed by id). Author of P2Pool decided it would have been a nice idea to attach ids to everything,
	so I cannot tell the difference anymore. To be called with non-null signature. */
	virtual void MangleMessageFromServer(const std::string &idstr, const char *signature, const rapidjson::Value &notification) = 0;

	/*! Sending and receiving data is left to a derived class. This call tries to send stratum blobs guaranteed to always be at least 1 byte.
	The send must be implemented in a non-blocking way, if no bytes can be sent right away, it can return 0
	as the number of bytes sent (it is not considered an error). In practice, if an error occurs, .first of returned value will be false.
    Sending and receiving data is a fairly rare occurance so take the chance to run some validation on the socket.
    \note This is called only when Refresh(canRead=true, x), which implies the socket is valid when this is called. */
	virtual std::pair<bool, asizei> Send(const abyte *data, const asizei count) throw() = 0;

	/*! \sa Send
    \note This is called only when Refresh(x, canWrite=true) */
	virtual std::pair<bool, asizei> Receive(abyte *storage, asizei rem) throw() = 0;

	std::unique_ptr<StratumState> stratum;

    //! Upon creation, AbstractWorkSources can be used in the sense they won't crash if you call their funcs but they don't do anything useful until
    //! some outer code builds a connection to server and initiates stratum handshaking. This is performed by creating a brand new stratum state.
    typedef std::vector< std::pair<const char*, const char*> > Credentials;
    void NewStratum(const Credentials &users);

    //! Call this when a connection goes down. This will destroy internal stratum state and NewStratum will have to be called at some point.
    void ClearStratum();

    //! Called by GetUserNames() when there's no stratum state object available to resolve this information.
    //! Implementations will return as_off as authorization status. Non-eligibility is assumed.
	virtual void GetCredentials(std::vector< std::pair<const char*, StratumState::AuthStatus> > &list) const = 0;

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

    void SetStratumCallbacks() {
        stratum->shareResponseCallback =  [this](asizei index, StratumShareResponse status) {
		    if(this->shareResponseCallback) this->shareResponseCallback(*this, index, status); // I honestly don't like std::bind so much
	    };
        stratum->workerAuthCallback = [this](const std::string &worker, StratumState::AuthStatus status) {
            if(this->workerAuthCallback) this->workerAuthCallback(*this, worker, status);
        };
    }
};
