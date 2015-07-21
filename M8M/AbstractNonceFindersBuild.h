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
2- register all algorithms to run on the various devices to be used.
This is the "hub" where multiple threads (one for each device) converge to talk to the caller thread.
No need to deal with dispatchers or anything. The threads will asynchronously look at the state inside here and work or sleep accordingly.
As this class is quite easy, I also specify some basic thread interface for the miners. */
class AbstractNonceFindersBuild : public NonceFindersInterface {
public:
    /*! Register all sources which will provide work to this object. Those sources should all use the same algorithm, which in turn it's the same algo
    mangled by the various dispatchers. In other words, given an arbitrary registered source S, producing work W, dispatching W to an arbitrary Dispatcher D
    is a valid operation producing good results. 
    Those calls must come first and before any call to InitWorkQueue(...). */
    bool RegisterWorkProvider(const AbstractWorkSource &src){
        const void *key = &src; // I drop all type information so I don't run the risk to try access this async
        auto compare = [key](const CurrentWork &test) { return test.owner == key; };
        if(std::find_if(owners.cbegin(), owners.cend(), compare) != owners.cend()) return false; // already added. Not sure if this buys anything but not a performance path anyway
        CurrentWork source(src.diffMul);
        source.owner = key;
        owners.push_back(std::move(source));
        return true;
    }
    
    struct AlgoBuild {
        SignedAlgoIdentifier identifier;
        std::vector<AbstractAlgorithm::ResourceRequest> res;
        std::vector<AbstractAlgorithm::KernelRequest> kern;
        asizei numHashes = 0;
        cl_context ctx;
        cl_device_id dev; 
        asizei candHashUints = 0;
    };

    /*! Initialize a mining thread using the passed device. Contents of the own parameter will be moved to internal memory. */
    void GenQueue(AlgoBuild &own, AbstractAlgorithm::SourceCodeBufferGetterFunc loader
#if defined REPLICATE_CLDEVICE_LINEARINDEX
                , asizei devLinearIndex
#endif
                ) {
        miners.push_back(std::move(std::unique_ptr<Miner>(new Miner)));
        ScopedFuncCall clear([this]() { miners.pop_back(); });
        auto miningThreadFunc = GetMiningMain();
        miners.back()->worker = std::thread(miningThreadFunc, std::ref(*miners.back()), miners.size() - 1, loader, own
#if defined REPLICATE_CLDEVICE_LINEARINDEX
                , devLinearIndex
#endif
        );
        miners.back()->worker.detach();
        clear.Dont();
    }

    /*! Map a cl_device_id to a device linearIndex for feedback. If not found, -1 will be used.
    Again, populated at construction time and supposed to be never, ever touched again if not by async thread so not thread protected. */
    std::map<cl_device_id, auint> linearDevice;

    /*! This function is called every time an algorithm completes, regardless it produces a nonce or not, valid or not.
    It's going to be called asynchronously so it must be appropriately synchronized.
    It is assumed iterations always take at least one microseconds. Elapsed=0 can be used to signal device going to sleep. */
    std::function<void(asizei devIndex, bool found, std::chrono::microseconds elapsed)> onIterationCompleted;

    // Those are not really part of initialization but the class is still fairly easy.
    bool SetDifficulty(const AbstractWorkSource &from, const stratum::WorkDiff &diff) {
        std::unique_lock<std::mutex> lock(guard);
        auto match(std::find_if(owners.begin(), owners.end(), [&from](const CurrentWork &test) { return test.owner == &from; }));
        if(match == owners.end()) return false;
        match->workDiff = diff;
        return true;
    }
    bool SetWorkFactory(const AbstractWorkSource &from, std::unique_ptr<stratum::AbstractWorkFactory> &factory) {
        std::unique_lock<std::mutex> lock(guard);
        auto match(std::find_if(owners.begin(), owners.end(), [&from](const CurrentWork &test) { return test.owner == &from; }));
        if(match == owners.end()) return false;
        AddFactory(factory.get());
        if(match->factory) RemFactory(match->factory);
        match->factory = factory.release();
        return true;
    }


    bool ResultsFound(NonceOriginIdentifier &src, VerifiedNonces &nonces) {
        std::unique_lock<std::mutex> lock(guard);
        if(results.empty()) return false;
        src = results.front().first;
        nonces = std::move(results.front().second);
        results.pop();
        return true;
    }


    std::array<asizei, 2> GetNumWorkQueues() const {
        // no need to protect as this is only updated at construction time
        //std::unique_lock<std::mutex> lock(guard);
        std::array<asizei, 2> count;
        count[0] = 0;
        count[1] = miners.size();
        for(asizei i = 0; i < miners.size(); i++) {
            auto &queue(*miners[i]);
            std::unique_lock<std::mutex> lock(queue.sync);
            if(Failed(queue)) count[0]++;
        }
        return count;
    }


    
    /*! For each work queue, this pulls out
    - <0> as the device (linear index) on which it's running
    - <1> is mostly to decide the meaning of the next
    - <2> is the last error message. Of course if everything is fine then there will be no message.
          If the thread is failed this is empty then we have an huge problem. */
    std::tuple<asizei, Status, std::vector<std::string>> GetTerminationReason(asizei queue) const {
        auto &worker(*miners[queue]);
        std::unique_lock<std::mutex> lock(worker.sync);
#if defined REPLICATE_CLDEVICE_LINEARINDEX
        asizei devIndex = worker.algo->linearDeviceIndex;
#else
        asizei devIndex = linearDevice.find(worker.algo->device)->second;
#endif
        return std::make_tuple(devIndex, worker.status, worker.exitMessage);
    }

    std::chrono::system_clock::time_point GetLastWUGenTime(asizei queue) const {
        auto &worker(*miners[queue]);
        std::unique_lock<std::mutex> lock(worker.sync);
        return worker.lastWUGen;
    }

    ~AbstractNonceFindersBuild() {
        keepRunning = false;
        using namespace std::chrono;
        const system_clock::time_point requested(system_clock::now());
        const std::chrono::milliseconds interval(200);
        while(system_clock::now() < requested + seconds(10) && exitedThreads != miners.size()) std::this_thread::sleep_for(interval);
        if(exitedThreads != miners.size()) {
            /* some threads are unresponsive. In theory we could terminate those but thread termination is dangerous in Windows
            and most likely in other OS as well (that's why C++11 does not have it by default).
            In theory we could at least signal that error but we won't, not just because we're in a DTOR but also because
            dead threads will have been detected before this is even run.

            So, let's just hope the threads are really, really dead and get the rid of the resources.
            If they wake up from the dead they'll bail out but perhaps that's the best thing to do.

            In my experience dead threads are a fairly rare occurance. They are due mostly to brittle drivers and when they
            die, they die in a OpenCL call to never return... so... 
            Basically we leak their stack-allocated resources. */
        }
    }

protected:
    /*! The most important property of work to be mangled is: who is generating this?
    Threads can switch to other pools at will and roll new work at will so those objects must be thread protected somehow. */
    struct CurrentWork {
        const void *owner;
        PoolInfo::DiffMultipliers diffMul;
        stratum::WorkDiff workDiff;
        stratum::AbstractWorkFactory *factory; //! this is owned by the usedFactories std::map, which keeps them reference-counted
        CurrentWork(PoolInfo::DiffMultipliers multipliers) : diffMul(multipliers), factory(nullptr) { }
    };
    template<typename Type>
    struct RefCounted {
        std::unique_ptr<Type> res;
        asizei count = 0;
        
        RefCounted(Type *own) : res(own) { }
        RefCounted<Type>(RefCounted<Type> &&other) {
            res = std::move(other.res);
            count = other.count;
        }
        RefCounted<Type>& operator=(RefCounted<Type> &&other) {
            if(this != &other) {
                count = other.count;
                res = std::move(other.res);
            }
            return *this;
        }
        bool operator==(const Type *other) const { return res.get() == other; }
        RefCounted<Type>& operator=(const RefCounted<Type> &) = delete;
        RefCounted<Type>(const RefCounted<Type> &other) = delete;
    };

    mutable std::mutex guard;
    std::vector<CurrentWork> owners;
    std::vector< RefCounted<stratum::AbstractWorkFactory> > usedFactories;
    /*!< This was originally a map but since there will be at worse dozens I take it easy and prefer easiness of tracking reference counting.
    All threads collaborate in setting the reference counter so this is also protected, it's used together with this->owners. */
    std::queue< std::pair<NonceOriginIdentifier, VerifiedNonces> > results;

    /*! One of those structs is generated for each thread so the objects themselves don't need to be thread-protected.
    In theory. In practice we still want to inquiry status of each thread to inspect for termination and whatever. */
    struct Miner {
        std::unique_ptr<AbstractAlgorithm> algo; //!< driven by this->dispatcher...
        std::unique_ptr<StopWaitDispatcher> dispatcher; //!< being run on this->worker...
        std::thread worker; //!< this is not really required but it's a good idea to keep those around
        struct HeapResourcesInterface {
            virtual ~HeapResourcesInterface() { }
        };
        std::unique_ptr<HeapResourcesInterface> heapResources; //!< this allows the main thread to release some stuff even for unresponsive threadsp

        // Those are rarely used but since each thread has its own, I cannot just sync on this->guard or I'll serialize everything, and they are hi-frequency
        std::mutex sync;
        std::chrono::system_clock::time_point lastUpdate; //! This is set when the thread starts so if this is 0 and status is s_created we're initializing.
        std::chrono::system_clock::time_point lastWUGen; //! Different from lastUpdate, last time an header was rolled. 
        Status status = s_created;
        std::vector<std::string> exitMessage;
        asizei sleepCount = 0; // this is used to trigger "signal device unused" notification once
    };
    std::vector< std::unique_ptr<Miner> > miners; //!< unique_ptr used so those objects are persistent and can be used directly by the threads.
    std::atomic<bool> keepRunning = true;
    std::atomic<auint> exitedThreads = 0; //!< This is used after a while to check how many threads exited before we destroy resources.

    typedef std::function<void(Miner&, asizei slot, AbstractAlgorithm::SourceCodeBufferGetterFunc, AlgoBuild &build                              
#if defined REPLICATE_CLDEVICE_LINEARINDEX
                , asizei devLinearIndex
#endif
        )> MiningMain;

    /*! Spawn a detached thread which will populate the passed structure and work on it, as well as the shared state.
    The threads need to generate all resources but don't need to clean them up, the caller thread takes care of doing that.
    The miner structure is passed in an guaranteed to be persistent at the index passed in our management pool but it's basically empty with no algo nor dispatcher. */
    virtual MiningMain GetMiningMain() = 0;

    void RemFactory(stratum::AbstractWorkFactory *old) {
        auto match(std::find_if(usedFactories.begin(), usedFactories.end(), [old](const RefCounted<stratum::AbstractWorkFactory> &test) {
            return test.res.get() == old;
        }));
        if(match == usedFactories.end()) return; // impossible anyway because of context
        match->count--;
        if(match->count == 0) usedFactories.erase(match);
    }

    //! Either add +1 to reference count or take ownership and add pointer to list of used factories. A bit ugly conceptually.
    stratum::AbstractWorkFactory* AddFactory(stratum::AbstractWorkFactory *ptr) {
        auto match(std::find_if(usedFactories.begin(), usedFactories.end(), [&ptr](const RefCounted<stratum::AbstractWorkFactory> &test) {
            return test.res.get() == ptr;
        }));
        if(match == usedFactories.end()) {
            RefCounted<stratum::AbstractWorkFactory> add(ptr);
            usedFactories.push_back(std::move(add));
            match = usedFactories.end() - 1;
        }
        match->count++;
        return match->res.get();
    }

    /*! Each mining thread must validate nonces by itself before reporting them. This helper struct will come in handy to track how hashes were generated.
    Each mining thread generates one on starting a new algo iteration. */
    struct NonceValidation {
        NonceOriginIdentifier generator; //!< not really required for validation but handy for sending
        adouble network;
        adouble target;
        auint nonce2;
        std::array<aubyte, 80> header;
    };

    //! Called on already locked object
    bool Failed(const Miner &miner) const {
        auto now(std::chrono::system_clock::now());
        switch(miner.status) {
        case s_sleeping: return false; // a sleeping thread is not dead... yet! OpenCL calls are the only unreliable things (on brittle drivers)
        case s_running: return now > miner.lastUpdate + std::chrono::seconds(5);
        }
        return miner.exitMessage.size() != 0;
    }
};
