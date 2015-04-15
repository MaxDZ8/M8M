/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/AbstractWorkSource.h"
#include "NonceStructs.h"
#include "../Common/Stratum/Work.h"


/*! A set of devices working to find nonces. All devices must run an algorithm or algorithm implementation compatible with an hash verifier method
provided by a derived class.
This base class contains the minimal interface to manage a running set of devices. Building the set is left to a derived class. 

Derived classes are expected to be populated by the main thread but once transitioned to a "running" state, they get their own thread.
Thereby, this interface must be thread safe / locked. */
class NonceFindersInterface {
public:
	virtual ~NonceFindersInterface() { }

    /*! Once a pool has been registered, this call allows to change the mining difficulty. Implementations are likely to apply the new diff
    only to the new work units generated.
    \note The "from" parameter is an AbstractWorkSource but it's owned by the caller, unprotected, persistent. Implementations should not use
    it if not for pointer comparison --> cast them to void* ASAP.
    \return false if owner is not being mangled by this set of devices. */
    virtual bool SetDifficulty(const AbstractWorkSource &from, const stratum::WorkDiff &diff) = 0;

    /*! Mining is the process of testing 80-bytes block headers to see if they hash to a number bigger than difficulty. More or less.
    Those block headers are generated by the mining thread independantly, it only needs a factory encapsulating the details.
    The miner thread will get full ownership of the factory, consider it gone forever. */
    virtual bool SetWorkFactory(const AbstractWorkSource &from, std::unique_ptr<stratum::AbstractWorkFactory> &factory) = 0;

    /*! Call this whatever possible to pull out a set of results if found. Those results are guaranteed to be valid and checked to be over provided
    difficulty target however, the generating block might have become stale. Objects implementing this interface should not be concerned about
    filtering stale work, this concern belongs to someone else. Results can accumulate over time which means in theory this shall be called in a loop.
    This returns true even if no nonces are really eligible to send, that it, it reports under-target and "hw errors" by returning an empty VerifiedNonces.
    \param [out] src A value passed to RefreshBlockData, used to identify where to send the produced nonces, if any.
    \param [out] nonces Results produced. nonces.nonces can have length 0 but nonces.nonces.length() + nonces.discarded + nonces.wrong must be always
        > 0 on returning true. If this does not hold, just return false and update nothing. */
    virtual bool ResultsFound(NonceOriginIdentifier &src, VerifiedNonces &nonces) = 0;

    enum Status {
        s_created,      //!< created, Init() not called yet. Initialization and creation is derived class responsability!
        s_initialized,  //!< Init() has been called and successfully completed with no errors. Start() not yet called.
        s_running,      //!< Start() has been called and we speculate everything is fine
        s_unresponsive, //!< bad stuff. Some watchdog have not been re-set too long, probably a good idea to kill this.
        s_stopped,      //!< Stop has been called.
        
        s_initFailed,
        s_terminated
    };

    //! This can have side-effects, such as testing for dead threads and update status itself. For this reason, it's not Get.
    //! If s_terminated is returned, GetTerminationReason will attempt to return meaningful data.
    virtual Status TestStatus() = 0;

    //! Returns non-empty string describing the reason 
    virtual std::string GetTerminationReason() const = 0;
};
