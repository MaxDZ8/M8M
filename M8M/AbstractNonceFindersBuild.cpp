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


bool AbstractNonceFindersBuild::RefreshBlockData(const NonceOriginIdentifier &from, std::unique_ptr<stratum::AbstractWorkUnit> &wu) {
    std::unique_lock<std::mutex> lock(guard);
    for(auto &el : owners) {
        if(el.owner == from.owner) {
            el.wu = std::move(wu);
            el.updated = true;
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
    while(!owners[something].wu) something++;

    // this always finds something as always called AFTER GottaWork.
    auto valinfo(Dispatch(dst, *owners[something].wu, owners[something].owner));
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
        if(mangling[loop] == nullptr) continue;
        if(mangling[loop]->updated == false) continue;
        if(mangling[loop]->wu.get() == nullptr) {
            // In theory I should stop the algorithm somehow but in practice this should never happen so
            throw "Attempting to map an empty WU to a dispatcher. Something has gone awry.";
        }
        flying.reserve(flying.size() + 1);
        auto valinfo(Dispatch(*algo[loop], *mangling[loop]->wu, mangling[loop]->owner));
        if(mangling[loop]->wu->restart) algo[loop]->algo.Restart();
        flying.push_back(valinfo);
    }
    for(auto &el : owners) el.updated = false;
}


bool AbstractNonceFindersBuild::IsCurrent(const NonceOriginIdentifier &what) const {
    std::unique_lock<std::mutex> lock(guard);
    for(const auto &test : owners) {
        if(test.owner == what.owner) {
            if(test.wu) return test.wu->job == what.job;
            return false; // Impossible, if here.
        }
    }
    return false;
}

bool AbstractNonceFindersBuild::GottaWork() const {
    std::unique_lock<std::mutex> lock(guard);
    asizei count = 0;
    for(auto &el : owners) if(el.wu) count++;
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


AbstractNonceFindersBuild::NonceValidation AbstractNonceFindersBuild::Dispatch(StopWaitDispatcher &target, stratum::AbstractWorkUnit &wu, const void *owner) {
    wu.MakeNoncedHeader(target.algo.BigEndian() == false); // ugly copypasta from Feed
    adouble netDiff = .0;
    if(wu.nonce2 == 0) netDiff = wu.ExtractNetworkDiff(target.algo.BigEndian() == false, target.algo.GetDifficultyNumerator());
    netDiff = wu.networkDiff;
    wu.nonce2++;

    std::array<aubyte, 80> header;
    for(asizei cp = 0; cp < header.size(); cp++) header[cp] = wu.header[cp];
    target.BlockHeader(header);

    const aubyte *blob = reinterpret_cast<const aubyte*>(wu.target.data());
	const aulong targetBits = *reinterpret_cast<const aulong*>(blob + 24);
    target.TargetBits(targetBits);

    return { NonceOriginIdentifier(owner, wu.job), netDiff, wu.shareDiff, wu.nonce2 - 1, header };
}
