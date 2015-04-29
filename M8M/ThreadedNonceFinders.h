/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractNonceFindersBuild.h"
#include <functional>
#include "CLEventGuardian.h"
#include <algorithm>


class ThreadedNonceFinders : public AbstractNonceFindersBuild {
public:
    ThreadedNonceFinders(const std::function<void(auint ms)> &sleep) : sleepFunc(sleep) { mangling.resize(owners.size()); }
    ~ThreadedNonceFinders() {
        if(pumper) {
            keepWorking = false;
            pumper->join(); // is that too risky? Originally, there was a Stop call so dtor was trivial...
        }
    }

    void Start() {
        std::unique_lock<std::mutex> lock(guard);
        if(status == s_initialized) {
            auto pumpFunc(GetMiningThread());
            pumper = std::make_unique<std::thread>(pumpFunc); // note with lambda captures we can be very very thread-safe, type-safe etc
            status = s_running;
        }
    }

protected:
    virtual std::array<aubyte, 32> HashHeader(std::array<aubyte, 80> &header, auint nonce) const = 0;

private:
    std::unique_ptr<std::thread> pumper;
    std::function<void(auint)> sleepFunc;
    std::atomic<bool> keepWorking = true;
    CLEventGuardian wait;
    std::set<cl_event> watched; //!< list of all events currently living somewhere in this->wait, which requires them to be unique

    std::vector<NonceValidation> flying;
    std::vector<const CurrentWork*> mangling; //!< shared pointers to the CurrentWork structure being mangled, to detect changes, one for each dispatcher

    MiningThreadFunc GetMiningThread() { return [this]() { MiningThread(); }; }

    void MiningThread() {
        const auint SLEEP_MS = 50;
        std::set<cl_event> triggered;
        bool first = true;
        try {
            std::vector<bool> algoWaiting(algo.size());
            std::vector<std::chrono::system_clock::time_point> algoStart(algo.size());
            std::vector<aubyte> signalCompletion(algo.size()); // first few iterations might be off, avoid signalling
            while(keepWorking) {
                if(GottaWork()) {
                    if(first) {
                        // First of all, feed all algorithms the first time.
                        for(asizei init = 0; init < algo.size(); init++) {
                            auto valid(Feed(*algo[init]));
                            flying.push_back(valid);
                        }
                        first = false;
                    }
                    else UpdateDispatchers(flying);
                    for(asizei loop = 0; loop < algo.size(); loop++) {
                        algoWaiting[loop] = MiningThreadPump(algoStart[loop], signalCompletion[loop], *algo[loop], triggered);
                    }
                    auto add(wait());
                    for(auto &el : add) { // anyway, those are removed from watch
                        watched.erase(el.first);
                        if(el.second != CL_SUCCESS) throw "Miner thread event waiting failed with error " + std::to_string(el.second);
                        triggered.insert(el.first);
                    }
                }
                else sleepFunc(SLEEP_MS);
            }
        } catch(std::exception ohno) {
            AbnormalTerminationSignal(ohno.what());
        } catch(const char *ohno) {
            AbnormalTerminationSignal(ohno);
        } catch(std::string ohno) {
            AbnormalTerminationSignal(ohno.c_str());
        } catch(...) {
            AbnormalTerminationSignal("Mining thread terminated due to unknown exception.");
        }
    }

    //! \return true if the dispatcher is stalled waiting for results (not equivalent to test waiting set as those are unique).
    bool MiningThreadPump(std::chrono::high_resolution_clock::time_point &started, aubyte &completed, StopWaitDispatcher &dispatcher, std::set<cl_event> &triggered) {
        bool waitResults = false;
        auto what = dispatcher.Tick(triggered);
        switch(what) {
            case AlgoEvent::dispatched: {
                started = std::chrono::system_clock::now();
            } break;
            case AlgoEvent::exhausted: {
                auto valid(Feed(dispatcher));
                flying.push_back(valid);
            } break;
            case AlgoEvent::working: {
                std::vector<cl_event> blockers;
                dispatcher.GetEvents(blockers);
                for(auto &ev : blockers) {
                    if(watched.find(ev) == watched.cend()) {
                        watched.insert(ev);
                        ScopedFuncCall clear([ev, this]() { watched.erase(ev); });
                        wait.Watch(ev);
                        clear.Dont();
                    }
                }
                waitResults = true;
                // Since we're gonna wait, take the chance to clear the header cache.
                for(asizei check = 0; check < flying.size(); check++) {
                    const auto &header(flying[check].header);
                    auto search = [&header](const std::unique_ptr<StopWaitDispatcher> &test) { return test->IsInFlight(header); };
                    if(std::any_of(algo.cbegin(), algo.cend(), search) == false) {
                        std::swap(flying[check], flying[flying.size() - 1]);
                        flying.pop_back();
                        check--;
                    }
                }
            } break;
            case AlgoEvent::results: {
                TickStatus();
                auto produced(dispatcher.GetResults()); // we know header already!
#if defined REPLICATE_CLDEVICE_LINEARINDEX // ugly hack to support device replication which adds non-unique cl_device_id, preventing map to work
                auto quirky(std::make_pair(dispatcher.algo.device, dispatcher.algo.linearDeviceIndex));
                auto match(&quirky);
#else
                auto match(linearDevice.find(dispatcher.algo.device));
#endif
                auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - started);
                if(completed < 16) completed++;
                else if(onIterationCompleted) onIterationCompleted(match->second, produced.nonces.size() != 0, elapsed);
                if(produced.nonces.empty()) break;
                auto matchPred = [&produced](const NonceValidation &test) { return test.header == produced.from; };
                auto dispatch(*std::find_if(flying.cbegin(), flying.cend(), matchPred));
                auto verified(CheckResults(dispatcher.algo.uintsPerHash, produced, dispatch)); // note with stop-n-wait dispatchers there is only one possible match so a set will suffice
#if defined REPLICATE_CLDEVICE_LINEARINDEX // ugly hack to support device replication which adds non-unique cl_device_id, preventing map to work
                verified.device = dispatcher.algo.linearDeviceIndex;
#else
                if(match == linearDevice.cend()) verified.device = asizei(-1);
                else verified.device = match->second;
#endif
                verified.nonce2 = dispatch.nonce2;
                if(verified.Total()) Found(dispatch.generator, verified);
            } break;
        }
        return waitResults;
    }

    VerifiedNonces CheckResults(asizei uintsPerHash, const MinedNonces &found, const NonceValidation &input) const {
        VerifiedNonces verified;
        verified.targetDiff = input.target;
        auto match = [&input](const CurrentWork &test) { return test.owner == input.generator.owner; };
        const auto diffMul(std::find_if(owners.cbegin(), owners.cend(), match)->diffMul);
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
            if(shareDiff >= input.network * shareDiff * shareDiff) good.block = true;
            verified.nonces.push_back(good);
        }
        return verified;
    }

	static double ResDiff(const std::array<aubyte, 32> &hash, double diffMul) {
		const double numerator = diffMul * btc::TRUE_DIFF_ONE;
		std::array<unsigned __int64, 4> copied;
		memcpy_s(copied.data(), sizeof(copied), hash.data(), 32);
		const double divisor = btc::LEToDouble(copied);
		return numerator / divisor;
	}
};
