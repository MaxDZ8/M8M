/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "NonceFindersInterface.h"
#include "../BlockVerifiers/BlockVerifierInterface.h"
#include "StopWaitDispatcher.h"
#include "../Common/AbstractWorkSource.h"
#include <mutex>
#include <queue>
#include <thread>
#include <chrono>

/*! Interface for populating, initializing and starting a NonceFindersInterface object 
Two things to do here:
1- register list of work providers
2- register all algorithms to run on this thread.
Remember this is basically a mining thread for a specific set of devices/algos.
In practice you don't even tick algorithms here: you put here dispatchers instead! 
To keep something private, this also defines some basic runtime behaviour.
\note RegisterWorkProvider, AddDispatcher and Init are supposed to be called only at initialization time (before Start()) so they don't lock threads! */
class AbstractNonceFindersBuild : public NonceFindersInterface {
public:
    /*! Register all sources which will provide work to this object. Those sources should all use the same algorithm, which in turn it's the same algo
    mangled by the various dispatchers. In other words, given an arbitrary registered source S, producing work W, dispatching W to an arbitrary Dispatcher D
    is a valid operation producing good results. */
    bool RegisterWorkProvider(const AbstractWorkSource &src);

    void AddDispatcher(std::unique_ptr<StopWaitDispatcher> &dispatcher) { 
        mangling.reserve(mangling.size() + 1);
        algo.push_back(std::move(dispatcher));
        mangling.push_back(nullptr);
    }

    /*! When all the sources and dispatchers have been put in, call this to begin creation of everything required by the algorithms.
    It's just a matter of iterating across all the algorithms and collecting their errors. 
    Optionally provide a vector to pull resource descriptors. If provided, each algo will Init() twice, first describing and then
    allocating. Allocation (real initialization) is only attempted if no errors are produced by description. */
    std::vector<std::string> Init(const std::string &loadPathPrefix, std::vector<AbstractAlgorithm::ConfigDesc> *resources = nullptr);

    //! Initiate async processing on another thread.
    virtual void Start() = 0;

    bool SetDifficulty(const AbstractWorkSource &from, const stratum::WorkDiff &diff);
    bool SetWorkFactory(const AbstractWorkSource &from, std::unique_ptr<stratum::AbstractWorkFactory> &factory);

    bool ResultsFound(NonceOriginIdentifier &src, VerifiedNonces &nonces);
    Status TestStatus();

    std::string GetTerminationReason() const {
        std::unique_lock<std::mutex> lock(guard);
        return terminationDesc;
    }

    //! map a cl_device_id to a device linearIndex for feedback. If not found, -1 will be used.
    //! Again, populated at construction time and supposed to be never, ever touched again if not by async thread so not thread protected.
    std::map<cl_device_id, asizei> linearDevice;
    

    //! This function is called every time an algorithm completes, regardless it produces a nonce or not, valid or not.
    //! It's going to be called asynchronously so it must be appropriately synchronized.
    typedef std::function<void(asizei devIndex, bool found, std::chrono::microseconds elapsed)> PerformanceMonitoringFunc;
    PerformanceMonitoringFunc onIterationCompleted;

protected:
    typedef std::function<void()> MiningThreadFunc;
    virtual MiningThreadFunc GetMiningThread() = 0;

    struct NonceValidation {
        NonceOriginIdentifier generator; //!< not really required for validation but handy for sending
        adouble network;
        adouble target;
        auint nonce2;
        std::array<aubyte, 80> header;
    };

    /*! Called by the asynchronous mining thread this function selects a WU from the list of current WUs and fetches its data
    to a certain dispatcher. This function might change dispatchers to different pools. */
    NonceValidation Feed(StopWaitDispatcher &dst);

    /*! Using the CurrentWork-to-Dispatch mappings estabilished by Feed(), check if the dispatched WU is stale and update it.
    If a WU has to be updated, the dispatcher will get a new header, which will be added to the list of "in flight" headers. */
    void UpdateDispatchers(std::vector<NonceValidation> &flying);

    /*! In general, each algorithm/dispatcher will just be Tick()'d until exhausting the nonce range.
    This however is not such a big idea if the pool gives up a new job. In that case, the dispatcher shall be considered
    exhausted and updated with new parameters.
    \returns True if I can just keep iterating on the already given data. */
    bool IsCurrent(const NonceOriginIdentifier &what) const;

    bool GottaWork() const;

    void Found(const NonceOriginIdentifier &owner, VerifiedNonces &magic);

    void TickStatus() { lastStatusUpdate = std::chrono::system_clock::now(); }

    //! Async mining thread calls this when something goes really wrong.
    void AbnormalTerminationSignal(const char *msg);

    struct CurrentWork {
        const void *owner;
        PoolInfo::DiffMultipliers diffMul;
        stratum::WorkDiff workDiff;
        std::unique_ptr<stratum::AbstractWorkFactory> factory;
        struct {
            bool work = false;
            bool diff = false;
        } updated;
        CurrentWork(PoolInfo::DiffMultipliers multipliers) : diffMul(multipliers) { }
        CurrentWork(const CurrentWork &nope) = delete;
        //CurrentWork(CurrentWork &&origin) = default; // not supported in VC2013
        CurrentWork(CurrentWork &&origin) {
            owner = origin.owner;
            diffMul = origin.diffMul;
            workDiff = origin.workDiff;
            factory.reset(origin.factory.release());
            updated = origin.updated;
        }
    };
    std::vector<CurrentWork> owners;

    //! Note this is not protected as it's really meant to be touched before the other thread is spawned or after it has been shut down and joined.
    std::vector< std::unique_ptr<StopWaitDispatcher> > algo;
    std::vector<CurrentWork*> mangling;

    mutable std::mutex guard;
    Status status = s_created;

private:
    std::chrono::system_clock::time_point lastStatusUpdate = std::chrono::system_clock::now();
    std::string terminationDesc;
    std::queue<std::pair<NonceOriginIdentifier, VerifiedNonces>> results;
    
    // The thread does not belong here! It is created in derived class to ensure it's destroyed at the right time.
    //std::unique_ptr<std::thread> pumper;

    static NonceValidation Dispatch(StopWaitDispatcher &target, const stratum::WorkDiff &diff, stratum::AbstractWorkFactory &factory, const void *owner);
};
