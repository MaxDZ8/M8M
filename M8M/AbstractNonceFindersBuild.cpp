/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "AbstractNonceFindersBuild.h"


bool AbstractNonceFindersBuild::RegisterWorkProvider(const AbstractWorkSource &src) {
    if(owners.empty() == false) throw "TODO: high frequency pool switching not supported yet!";
    //! \todo for the time being, only one supported, until I figure out how the policies driving Feed(StopWaitDispatcher)
    const void *key = &src; // I drop all information so I don't run the risk to try access this async
    auto compare = [key](const CurrentWork &test) { return test.owner == key; };
    if(std::find_if(owners.cbegin(), owners.cend(), compare) != owners.cend()) return false; // already added. Not sure if this buys anything but not a performance path anyway
    CurrentWork source(src.GetDiffMultipliers());
    source.owner = key;
    owners.push_back(std::move(source));
    return true;
}


std::vector<std::string> AbstractNonceFindersBuild::Init(const std::string &loadPathPrefix, std::vector<AbstractAlgorithm::ConfigDesc> *resources) {
    std::vector<std::string> ret;
    ScopedFuncCall badInit([this]() { status = s_initFailed; });
    for(auto &el : algo) {
        if(resources) {
            resources->push_back(AbstractAlgorithm::ConfigDesc());
            auto err(el->algo.Init(&resources->back(), el->AsValueProvider(), loadPathPrefix));
            for(auto &meh : err) ret.push_back(meh);
            if(err.size()) continue;
        }
        auto err(el->algo.Init(nullptr, el->AsValueProvider(), loadPathPrefix));
        for(auto &meh : err) ret.push_back(meh);
    }
    if(ret.empty()) {
        badInit.Dont();
        status = s_initialized;
    }
    return ret;
}


bool AbstractNonceFindersBuild::SetDifficulty(const AbstractWorkSource &from, const stratum::WorkDiff &diff) {
    std::unique_lock<std::mutex> lock(guard);
    for(auto &el : owners) {
        if(el.owner == &from) {
            el.workDiff = diff;
            el.updated.diff = true;
            return true;
        }
    }
    return false;
}


bool AbstractNonceFindersBuild::SetWorkFactory(const AbstractWorkSource &from, std::unique_ptr<stratum::AbstractWorkFactory> &factory) {
    std::unique_lock<std::mutex> lock(guard);
    for(auto &el : owners) {
        if(el.owner == &from) {
            if(el.factory && factory->restart == false) factory->Continuing(*el.factory);
            el.factory = std::move(factory);
            el.updated.work = true;
            return true;
        }
    }
    return false;
}


bool AbstractNonceFindersBuild::ResultsFound(NonceOriginIdentifier &src, VerifiedNonces &nonces) {
    std::unique_lock<std::mutex> lock(guard);
    if(results.empty()) return false;
    src = results.front().first;
    nonces = std::move(results.front().second);
    results.pop();
    return true;
}


NonceFindersInterface::Status AbstractNonceFindersBuild::TestStatus() {
    std::unique_lock<std::mutex> lock(guard);
    using namespace std::chrono;
#ifdef _DEBUG
    auto tolerance(duration_cast<system_clock::duration>(seconds(300)));
#else
    auto tolerance(duration_cast<system_clock::duration>(seconds(30)));
#endif
    if(lastStatusUpdate < system_clock::now() - tolerance) status = s_unresponsive;
    return status;
}


AbstractNonceFindersBuild::NonceValidation AbstractNonceFindersBuild::Feed(StopWaitDispatcher &dst) {
    //! \todo for the time being, only a single pool.
    std::unique_lock<std::mutex> lock(guard);
    //! For the time being, just pull work from the first pool having work.
    asizei something = 0;
    while(!owners[something].factory) something++;

    // this always finds something as always called AFTER GottaWork.
    auto valinfo(Dispatch(dst, owners[something].workDiff, *owners[something].factory, owners[something].owner));
    dst.algo.Restart();
    asizei slot;
    for(slot = 0; slot < algo.size(); slot++) {
        if(algo[slot].get() == &dst) break;
    }
    mangling[slot] = owners.data() + something;
    return valinfo;
}


void AbstractNonceFindersBuild::UpdateDispatchers(std::vector<NonceValidation> &flying) {
    std::unique_lock<std::mutex> lock(guard);
    for(asizei loop = 0; loop < algo.size(); loop++) {
        auto *el = mangling[loop];
        if(el == nullptr) continue;
        if(el->updated.work) {
            if(!el->factory) {
                // In theory I should stop the algorithm somehow but in practice this should never happen so
                throw "Attempting to map an empty WU to a dispatcher. Something has gone awry.";
            };
            flying.reserve(flying.size() + 1);
            auto valinfo(Dispatch(*algo[loop], el->workDiff, *el->factory, el->owner));
            flying.push_back(valinfo);
        }
        else if(el->updated.diff) { // do this after work, as Dispatch already takes care of re-setting it.
            algo[loop]->TargetBits(el->workDiff.target[3]);
        }
    }
    for(auto &current : mangling) { // let's take it easy
        if(!current) continue;
        current->updated.diff = current->updated.work = false;
    }
}


bool AbstractNonceFindersBuild::IsCurrent(const NonceOriginIdentifier &what) const {
    std::unique_lock<std::mutex> lock(guard);
    for(const auto &test : owners) {
        if(test.owner == what.owner) {
            if(test.factory) return test.factory->job == what.job;
            return false; // Impossible, if here.
        }
    }
    return false;
}

bool AbstractNonceFindersBuild::GottaWork() const {
    std::unique_lock<std::mutex> lock(guard);
    asizei count = 0;
    for(auto &el : owners) if(el.factory) count++;
    return count != 0;
}

void AbstractNonceFindersBuild::Found(const NonceOriginIdentifier &owner, VerifiedNonces &magic) {
    std::unique_lock<std::mutex> lock(guard);
    results.push(std::make_pair(owner, std::move(magic)));
}


void AbstractNonceFindersBuild::AbnormalTerminationSignal(const char *msg) {
    std::unique_lock<std::mutex> lock(guard);
    status = s_terminated;
    try { terminationDesc = msg; } catch(...) { }
}


AbstractNonceFindersBuild::NonceValidation AbstractNonceFindersBuild::Dispatch(StopWaitDispatcher &target, const stratum::WorkDiff &diff, stratum::AbstractWorkFactory &factory, const void *owner) {
    auto work(factory.MakeNoncedHeader(target.algo.BigEndian() == false, target.algo.GetDifficultyNumerator()));
    adouble netDiff = factory.GetNetworkDiff();

    std::array<aubyte, 80> header;
    for(asizei cp = 0; cp < header.size(); cp++) header[cp] = work.header[cp];
    target.algo.Restart();
    target.BlockHeader(header);

    target.TargetBits(diff.target[3]);
    //! \todo this function should take care of new work only and leave targetbits independant

    return { NonceOriginIdentifier(owner, work.job), netDiff, diff.shareDiff, work.nonce2, header };
}
