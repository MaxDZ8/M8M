#include "ThreadedNonceFinders.h"


std::function<void(ThreadedNonceFinders::MiningThreadParams)> ThreadedNonceFinders::GetMiningMain() {
    return [this](MiningThreadParams meh) {
        auto &self(meh.miner);
        auto &build(meh.build);
        const auto index(meh.slot);
        const auto loader(meh.sourceGetter);
#if defined REPLICATE_CLDEVICE_LINEARINDEX
        const auto devLinearIndex(meh.devLinearIndex);
#endif
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


void ThreadedNonceFinders::MiningPump(Miner &self, ThreadResources &heap) {
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
            heap.algoStarted = false;
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


void ThreadedNonceFinders::PumpDispatcher(Miner &self, ThreadResources &heap, bool newWork, bool newDiff) {
    static LARGE_INTEGER counterFrequency { 0 };
    if(!counterFrequency.QuadPart) {
        if(!QueryPerformanceFrequency(&counterFrequency)) {
            throw "Pre-XP OS, not supported"; // oh cmon, that won't really happen!
        }
    }

    using namespace std::chrono;
    auto &dispatcher(*self.dispatcher);
    if(!heap.algoStarted) Feed(self, heap, newWork, newDiff);

    auto what = dispatcher.Tick(heap.waiting);
    switch(what) {
        case AlgoEvent::dispatched: 
            heap.algoStarted = true;
            QueryPerformanceCounter(&heap.startTick);
            break;
        case AlgoEvent::exhausted: Feed(self, heap, true, newDiff);    break;
        case AlgoEvent::working: {
            dispatcher.GetEvents(heap.waiting);
            // Since we're gonna wait, take the chance to clear the header cache.
            auto pack { std::remove_if(heap.flying.begin(), heap.flying.end(), [&self](auto &item) {
                return self.dispatcher->IsInFlight(item.header) == false;
            })};
            heap.flying.erase(pack, heap.flying.end());
            clWaitForEvents(cl_uint(heap.waiting.size()), heap.waiting.data());
        } break;
        case AlgoEvent::results: {
            TickStatus(self);
            auto produced(dispatcher.GetResults()); // we know header already!
            const auto devLinear(GetDeviceLinearIndex(dispatcher));
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            auto elapsedus = now.QuadPart - heap.startTick.QuadPart;
            elapsedus *= 1000000;
            elapsedus /= counterFrequency.QuadPart;
            if(heap.iterations < 16) heap.iterations++;
            else if(onIterationCompleted) onIterationCompleted(devLinear, produced.nonces.size() != 0, microseconds(elapsedus));
            if(produced.nonces.empty()) break;
            auto matchPred = [&produced](const NonceValidation &test) { return test.header == produced.from; };
            auto dispatch(*std::find_if(heap.flying.cbegin(), heap.flying.cend(), matchPred));
            auto verified(CheckResults(dispatcher.algo.uintsPerHash, produced, dispatch)); // note with stop-n-wait dispatchers there is only one possible match so a set will suffice
            verified.device = devLinear;
            verified.nonce2 = dispatch.nonce2;
            if(verified.Total()) Found(dispatch.generator, verified);
            heap.algoStarted = false;
        } break;
    }
}


void ThreadedNonceFinders::Feed(Miner &self, ThreadResources &heap, bool newWork, bool newDiff) {
    adouble netDiff = heap.myWork->GetNetworkDiff();
    bool generated = false;
    if(newWork) {
        heap.current = heap.myWork->MakeNoncedHeader(self.canon.bigEndian == false, self.canon.diffNumerator);
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


VerifiedNonces ThreadedNonceFinders::CheckResults(asizei uintsPerHash, const MinedNonces &found, const NonceValidation &input) const {
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
