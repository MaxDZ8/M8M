/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractNonceFindersBuild.h"
#include <functional>
#include <algorithm>
#include "DataDrivenAlgorithm.h"

#ifdef _WIN32
#include <Windows.h>
#endif


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
        std::chrono::system_clock::time_point workValidated;
#if defined _WIN32
        bool algoStarted = false;
        LARGE_INTEGER startTick; //!< in theory, QPC might return 0 as value so guard this
#endif

        stratum::AbstractWorkFactory *myWork = nullptr;
        const void *owner = nullptr;
        stratum::Work current;
        stratum::WorkDiff diff;
        std::array<aubyte, 80> header; //!< header to dispatch at next Feed. It is kept so when diff changes we don't regen.

        std::vector<NonceValidation> flying;

        std::vector<cl_event> waiting;
        asizei iterations = 0;
    };

    std::function<void(MiningThreadParams)> GetMiningMain();

    //! In this function I mostly take care of the work to mangle. Once decided what to do PumpDispatcher is the real deal.
    void MiningPump(Miner &self, ThreadResources &heap);


    void PumpDispatcher(Miner &self, ThreadResources &heap, bool newWork, bool newDiff);

    //! Start a new algorithm iteration. This means updating new data to device and remember the validation data.
    void Feed(Miner &self, ThreadResources &heap, bool newWork, bool newDiff);


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

    VerifiedNonces CheckResults(asizei uintsPerHash, const MinedNonces &found, const NonceValidation &input) const;

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
