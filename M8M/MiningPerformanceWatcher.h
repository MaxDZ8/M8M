/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <chrono>
#include <vector>
#include <mutex>

class MiningPerformanceWatcherInterface {
public:
    typedef std::chrono::microseconds microseconds;
	virtual ~MiningPerformanceWatcherInterface() { }

    virtual size_t GetNumDevices() const = 0;

	struct DevStats {
        //! Main type of performance monitoring involves taking averages across GetAverageWindow() seconds.
		std::chrono::microseconds avg;

        //! Every time a nonce is produced (regardless of its validity) this value is updated with the time taken to produce it.
        //! Non-existant devices produce 0, as well as probing before a nonce is found.
		std::chrono::microseconds last;

        //! Max and Min iteration time are also tracked. Implementations can start tracking those after reaching performance stability.
		std::chrono::microseconds min, max;
	};

    //! The watcher collects performance samples over this amount of seconds and then produces an average.
    virtual std::chrono::seconds GetAverageWindow() const = 0;

    //! Returns false if device >= GetNumDevices or if performance cannot be yet inspected.
    virtual bool GetPerformance(DevStats &out, size_t device) const = 0;
};


class MiningPerformanceWatcher : public MiningPerformanceWatcherInterface {
    std::vector<DevStats> stats;

    struct Info {
        std::chrono::system_clock::time_point start;
        size_t iterations = 0;
        bool used = false;
    };
    std::vector<Info> info;

public:
    std::chrono::seconds averageWindow;

    explicit MiningPerformanceWatcher(std::chrono::seconds twindow = std::chrono::seconds(1)) : averageWindow(twindow) { }

    void SetNumDevices(size_t count) {
        stats.resize(count);
        info.resize(count);
    }
    void Completed(size_t devIndex, bool found, std::chrono::microseconds elapsed) {
        using namespace std::chrono;
        auto &dev(stats[devIndex]);
        auto &collect(info[devIndex]);
        collect.used = true;
        auto now(system_clock::now());
        if(collect.start == system_clock::time_point()) collect.start = now;
        collect.iterations++;

        std::chrono::microseconds zero;
        if(dev.min == zero) dev.min = elapsed; // the assumption here is that everything will take at least 1 us.
        else if(elapsed < dev.min) dev.min = elapsed;
        if(dev.max == zero) dev.max = elapsed;
        else if(elapsed > dev.max) dev.max = elapsed;

        if(found) {
            dev.last = elapsed;
            microseconds total = duration_cast<microseconds>(now - collect.start);
            if(total >= duration_cast<microseconds>(averageWindow)) {
                dev.avg = duration_cast<microseconds>(total / double(collect.iterations));
                collect.start = system_clock::time_point();
                collect.iterations = 0;
            }
        }
    }
    
    // base class
    size_t GetNumDevices() const { return stats.size(); }
    bool GetPerformance(DevStats &out, size_t dev) const {
        if(dev < stats.size()) {
            if(info[dev].used == false) return false;
            out = stats[dev];
        }
        return dev < stats.size();
    }
    std::chrono::seconds GetAverageWindow() const { return averageWindow; }
};


 class SyncMiningPerformanceWatcher : public MiningPerformanceWatcher {
    mutable std::mutex lock;
    typedef MiningPerformanceWatcher base;
public:
    void SetNumDevices(asizei count) {
        std::unique_lock<std::mutex> sync(lock);
        base::SetNumDevices(count);
    }
    void Completed(size_t devIndex, bool found, std::chrono::microseconds elapsed) {
        std::unique_lock<std::mutex> sync(lock);
        base::Completed(devIndex, found, elapsed);
    }
    size_t GetNumDevices() const {
        std::unique_lock<std::mutex> sync(lock);
        return base::GetNumDevices();
    }
    bool GetPerformance(DevStats &out, size_t dev) const {
        std::unique_lock<std::mutex> sync(lock);
        return base::GetPerformance(out, dev);
    }
    std::chrono::seconds GetAverageWindow() const {
        std::unique_lock<std::mutex> sync(lock);
        return base::GetAverageWindow();
    }
};
