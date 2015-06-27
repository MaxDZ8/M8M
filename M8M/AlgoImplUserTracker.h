/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <string>
#include "AbstractAlgorithm.h"


/*! This interface is used by the miner to signal which threads are pulling up which sources.
The miner registers an user of a certain algo-impl for each starting thread.
This pointer is also used - synchronously - to let the mapper know when threads have initialized.
When a thread is detected as initialized, the corresponding entry will be marked as initialized so outer code
can use it as a notification to drop the sources, which are now unnecessary.

The mining threads are the "users" of the various Algorithm implementations. */
class AlgoImplUserTracker {
public:
    virtual ~AlgoImplUserTracker() { }
    void AddUser(const std::string &algo, const std::string &impl) {
        workQueues.push_back(AlgoImplUser(algo, impl));
        // Note those are created in order, they can complete out-of-order but it is expected count to be coherent with
        // miner enumeration of work queues somehow.
    }
    void Initialized(asizei index) {
        if(index < workQueues.size()) workQueues[index].initialized = true;
    }
    asizei GetNumRegisteredConsumers() { return workQueues.size(); }
    asizei GetNumInitializedConsumers() {
        return std::count_if(workQueues.cbegin(), workQueues.cend(), [](const AlgoImplUser &test) {
            return test.initialized;
        });
    }

private:
    struct AlgoImplUser {
        std::string algo;
        std::string impl;
        bool initialized = false; //!< the order in which work queues initialize it's not really important. What really matters is when they're all initialized.
        AlgoImplUser() = default;
        AlgoImplUser(const std::string a, const std::string i) : algo(a), impl(i) { }
    };
    std::vector<AlgoImplUser> workQueues;
};
