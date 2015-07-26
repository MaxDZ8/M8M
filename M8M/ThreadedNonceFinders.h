/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractNonceFindersBuild.h"
#include <functional>
#include <algorithm>
#include "DataDrivenAlgorithm.h"


class ThreadedNonceFinders : public AbstractNonceFindersBuild {
protected:
    virtual std::array<aubyte, 32> HashHeader(std::array<aubyte, 80> &header, auint nonce) const = 0;

    struct WorkInfo {
        stratum::AbstractWorkFactory *work;
        stratum::WorkDiff diff;
        const void *owner;
        explicit WorkInfo() { owner = nullptr;    work = nullptr; }
        WorkInfo(stratum::AbstractWorkFactory *w, stratum::WorkDiff d, const void *o) : work(w), diff(d), owner(o) { }
    };

    struct PoolSelectionPolicyInterface {
        virtual ~PoolSelectionPolicyInterface() { }
        virtual WorkInfo Select(const std::vector<CurrentWork> &pools) = 0;
    };

    struct FirstWorkingPool : PoolSelectionPolicyInterface {
        WorkInfo Select(const std::vector<CurrentWork> &pools) {
            for(auto &pool : pools) {
                if(pool.factory) return WorkInfo(pool.factory, pool.workDiff, pool.owner);
            }
            return WorkInfo();
        }
    } psPolicy;

private:
    struct ThreadResources : Miner::HeapResourcesInterface {
        std::chrono::milliseconds sleepInterval, workValidationInterval;
        std::chrono::system_clock::time_point workValidated, algoStarted;

        stratum::AbstractWorkFactory *myWork = nullptr;
        const void *owner = nullptr;
        stratum::Work current;
        stratum::WorkDiff diff;
        std::array<aubyte, 80> header; //!< header to dispatch at next Feed. It is kept so when diff changes we don't regen.

        std::vector<NonceValidation> flying;

        std::vector<cl_event> waiting;
        asizei iterations = 0;
    };

    MiningMain GetMiningMain() {
        return [this](Miner &self, asizei index, AbstractAlgorithm::SourceCodeBufferGetterFunc loader, AlgoBuild &build
#if defined REPLICATE_CLDEVICE_LINEARINDEX
                , asizei devLinearIndex
#endif
        ) {
            {
                std::unique_lock<std::mutex> lock(self.sync);
                self.lastUpdate = std::chrono::system_clock::now();
            }
            ThreadResources *heap = nullptr;
            try {
                // DataDrivenAlgorithm *algo = new DataDrivenAlgorithm(numHashes, ctx, cldev, algoname, implname, version, candHashUints);
                DataDrivenAlgorithm *algo = new DataDrivenAlgorithm(build.numHashes, build.ctx, build.dev, build.candHashUints);
#if defined REPLICATE_CLDEVICE_LINEARINDEX
                algo->linearDeviceIndex = cl_uint(devLinearIndex);
#endif
                self.algo.reset(algo);
                algo->identifier = std::move(build.identifier);
                self.dispatcher.reset(new StopWaitDispatcher(*self.algo));
                heap = new ThreadResources;
                self.heapResources.reset(heap);
                heap->sleepInterval = std::chrono::milliseconds(500 + index * 50);
                heap->workValidationInterval = std::chrono::milliseconds(100 + index * 10);
                auto err(algo->Init(self.dispatcher->AsValueProvider(), loader, build.res, build.kern));
                if(err.size()) {
                    std::string conc;
                    for(auto &meh : err) conc += meh + '\n';
                    throw conc;
                }
                build.res.clear();
                build.kern.clear();
            }
            catch(std::exception ohno) { BadThings(self, s_initFailed, ohno.what()); }
            catch(const char *ohno)    { BadThings(self, s_initFailed, ohno); }
            catch(std::string ohno)    { BadThings(self, s_initFailed, ohno.c_str()); }
            catch(...)                 { BadThings(self, s_initFailed, "Mining thread failed to initialize due to unknown exception."); }
            if(self.status != s_created) return;
            self.status = s_running;

            try {
                while(keepRunning) {
                    MiningPump(self, *heap);
                    std::unique_lock<std::mutex> lock(self.sync);
                    if(self.status == s_running) self.lastUpdate = std::chrono::system_clock::now();
                }
            }
            catch(std::exception ohno) { BadThings(self, s_failed, ohno.what()); }
            catch(const char *ohno)    { BadThings(self, s_failed, ohno); }
            catch(std::string ohno)    { BadThings(self, s_failed, ohno.c_str()); }
            catch(...)                 { BadThings(self, s_failed, "Mining thread terminated due to unknown exception."); }
            exitedThreads++; // not quite yet but what can possibly go wrong at this point? It's the best we can do.
        };
    }

    //! In this function I mostly take care of the work to mangle. Once decided what to do PumpDispatcher is the real deal.
    void MiningPump(Miner &self, ThreadResources &heap) {
        bool newWork = false, newDiff = false;
        if(heap.myWork == nullptr) {
            std::unique_lock<std::mutex> lock(guard);
            auto use(psPolicy.Select(owners));
            heap.myWork = use.work;
            heap.owner = use.owner;
            if(heap.myWork) {
                AddFactory(heap.myWork);
                heap.workValidated = std::chrono::system_clock::now();
                newWork = true;
                if(heap.diff != use.diff) {
                    heap.diff = use.diff;
                    newDiff = true;
                }
                self.sleepCount = 0;
            }
            else { // still nothing to do
                auto devLinear(GetDeviceLinearIndex(*self.dispatcher));
                if(self.sleepCount == 1 && onIterationCompleted) onIterationCompleted(devLinear, false, std::chrono::microseconds(0));
                lock.unlock();
                std::unique_lock<std::mutex> pre(self.sync);
                self.status = s_sleeping;
                pre.unlock();
                std::this_thread::sleep_for(heap.sleepInterval);
                self.sleepCount++;
                std::unique_lock<std::mutex> post(self.sync);
                self.status = s_running;
                return;
            }
        }
        // Ok, I have work. Every once in a while, check if the work is already valid.
        if(std::chrono::system_clock::now() > heap.workValidated + heap.workValidationInterval) {
            std::unique_lock<std::mutex> lock(guard);
            auto match(std::find_if(owners.cbegin(), owners.cend(), [&heap](const CurrentWork &cw) { return cw.factory == heap.myWork; }));
            if(match != owners.cend()) heap.workValidated = std::chrono::system_clock::now();
            else { // I must get another one; easiest way is to just give up and the policy will get me one next time but handle the ref counting
                auto factory(std::find(usedFactories.begin(), usedFactories.end(), heap.myWork));
                self.dispatcher->Cancel(heap.waiting);
                heap.algoStarted = std::chrono::system_clock::time_point();
                heap.myWork = nullptr;
                RemFactory(factory->res.get());
                return;
            }
            // Also take the chance to update the work difficulty - the header data comes automatically from the factory
            if(heap.diff != match->workDiff) newDiff = true;
            heap.diff = match->workDiff;
        }
        PumpDispatcher(self, heap, newWork, newDiff);
    }

    void PumpDispatcher(Miner &self, ThreadResources &heap, bool newWork, bool newDiff) {
        using namespace std::chrono;
        auto &dispatcher(*self.dispatcher);
        if(heap.algoStarted == system_clock::time_point()) Feed(self, heap, newWork, newDiff);

        auto what = dispatcher.Tick(heap.waiting);
        switch(what) {
            case AlgoEvent::dispatched: heap.algoStarted = std::chrono::system_clock::now();    break;
            case AlgoEvent::exhausted: Feed(self, heap, true, newDiff);    break;
            case AlgoEvent::working: {
                dispatcher.GetEvents(heap.waiting);
                // Since we're gonna wait, take the chance to clear the header cache.
                for(asizei check = 0; check < heap.flying.size(); check++) {
                    const auto &header(heap.flying[check].header);
                    if(self.dispatcher->IsInFlight(header) == false) {
                        std::swap(heap.flying[check], heap.flying[heap.flying.size() - 1]);
                        heap.flying.pop_back();
                        check--;
                    }
                }
                clWaitForEvents(cl_uint(heap.waiting.size()), heap.waiting.data());
            } break;
            case AlgoEvent::results: {
                TickStatus(self);
                auto produced(dispatcher.GetResults()); // we know header already!
                const auto devLinear(GetDeviceLinearIndex(dispatcher));
                auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - heap.algoStarted);
                if(heap.iterations < 16) heap.iterations++;
                else if(onIterationCompleted) onIterationCompleted(devLinear, produced.nonces.size() != 0, elapsed);
                if(produced.nonces.empty()) break;
                auto matchPred = [&produced](const NonceValidation &test) { return test.header == produced.from; };
                auto dispatch(*std::find_if(heap.flying.cbegin(), heap.flying.cend(), matchPred));
                auto verified(CheckResults(dispatcher.algo.uintsPerHash, produced, dispatch)); // note with stop-n-wait dispatchers there is only one possible match so a set will suffice
                verified.device = devLinear;
                verified.nonce2 = dispatch.nonce2;
                if(verified.Total()) Found(dispatch.generator, verified);
                heap.algoStarted = system_clock::time_point();
            } break;
        }

    }

    //! Start a new algorithm iteration. This means updating new data to device and remember the validation data.
    void Feed(Miner &self, ThreadResources &heap, bool newWork, bool newDiff) {
        adouble netDiff = heap.myWork->GetNetworkDiff();
        bool generated = false;
        if(newWork) {
            auto info(AbstractWorkSource::GetCanonicalAlgoInfo(self.algo->Identify().algorithm.c_str()));
            heap.current = heap.myWork->MakeNoncedHeader(info.bigEndian == false, info.diffNumerator);
            for(asizei cp = 0; cp < heap.header.size(); cp++) heap.header[cp] = heap.current.header[cp];

            self.lastWUGen = std::chrono::system_clock::now();
            self.dispatcher->algo.Restart();
            self.dispatcher->BlockHeader(heap.header);
            generated = true;
        }
        if(newDiff) {
            self.dispatcher->TargetBits(heap.diff.target[3]);
            generated = true;
        }
        if(generated) {
            NonceValidation track { { heap.owner, heap.myWork->job }, netDiff, heap.diff.shareDiff, heap.current.nonce2, heap.header };
            heap.flying.push_back(std::move(track)); // not quite, but will be started right away
        }
    }


    void TickStatus(Miner &self) {
        std::unique_lock<std::mutex> lock(self.sync);
        self.lastUpdate = std::chrono::system_clock::now();
        self.status = s_running; // not really needed, it's already s_running because of construction!
    }


	static double ResDiff(const std::array<aubyte, 32> &hash, double diffMul) {
		const double numerator = diffMul * btc::TRUE_DIFF_ONE;
		std::array<unsigned __int64, 4> copied;
		memcpy_s(copied.data(), sizeof(copied), hash.data(), 32);
		const double divisor = btc::LEToDouble(copied);
		return numerator / divisor;
	}

    VerifiedNonces CheckResults(asizei uintsPerHash, const MinedNonces &found, const NonceValidation &input) const {
        VerifiedNonces verified;
        verified.targetDiff = input.target;
        auto match = [&input](const CurrentWork &test) { return test.owner == input.generator.owner; };
        std::unique_lock<std::mutex> lock(guard);
        const auto diffMul(std::find_if(owners.cbegin(), owners.cend(), match)->diffMul);
        lock.unlock();
        for(asizei test = 0; test < found.nonces.size(); test++) {
		    std::array<aubyte, 80> header; // hashers expect header in opposite byte order
		    for(auint i = 0; i < 80; i += 4) {
			    for(auint b = 0; b < 4; b++) header[i + b] = input.header[i + 3 - b];
		    }
            auto reference(HashHeader(header, found.nonces[test]));
            if(memcmp(reference.data(), found.hashes.data() + uintsPerHash * test, sizeof(reference))) {
                verified.wrong++;
                continue;
            }
            auto shareDiff(ResDiff(reference, diffMul.share));
            if(shareDiff <= input.target) {
                verified.discarded++;
                continue;
            }
		    // At this point I could generate the output like legacy mining apps:
		    //  <serverResult> <hashPart> Diff <shareDiff>/<wu.targetDiff> GPU <linearIndex> at <poolName>
            // But I cannot do that, instead, I must pack all the information so this can be produced when the nonce is either confirmed or rejected.
            VerifiedNonces::Nonce good;
            good.nonce = found.nonces[test];
            good.diff = shareDiff;
			const asizei HASH_DIGITS = good.hashSlice.size();
			asizei pull = 31;
			while(pull && reference[pull] == 0) pull--;
			if(pull < HASH_DIGITS) pull = HASH_DIGITS;
			for(asizei cp = 0; cp < HASH_DIGITS; cp++) good.hashSlice[cp] = reference[pull - cp];
            if(shareDiff >= input.network * diffMul.share * diffMul.share) good.block = true;
            verified.nonces.push_back(good);
        }
        return verified;
    }

    void Found(const NonceOriginIdentifier &owner, VerifiedNonces &magic) {
        std::unique_lock<std::mutex> lock(guard);
        results.push(std::make_pair(owner, std::move(magic)));
    }

    void BadThings(Miner &self, Status status, const char *msg) {
        std::unique_lock<std::mutex> lock(guard);
        self.status = status;
        self.exitMessage.push_back(msg);
    }

    auint GetDeviceLinearIndex(const StopWaitDispatcher &dispatcher) const {
#if defined REPLICATE_CLDEVICE_LINEARINDEX // ugly hack to support device replication which adds non-unique cl_device_id, preventing map to work
        auto quirky(std::make_pair(dispatcher.algo.device, dispatcher.algo.linearDeviceIndex));
        auto match(&quirky);
#else
        auto match(linearDevice.find(dispatcher.algo.device));
#endif
        return match->second;
    }
};
