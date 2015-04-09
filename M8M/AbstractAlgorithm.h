/*
 * Copyright (C) 2015 Massimo Del Zotto
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/AREN/ArenDataTypes.h"
#include <string>
#include <CL/cl.h>
#include <vector>
#include <map>
#include "KnownConstantsProvider.h"
#include "NonceStructs.h"
#include <array>
#include "../Common/AREN/ScopedFuncCall.h"
#include <fstream>
#include "../Common/hashing.h"
#include "AbstractSpecialValuesProvider.h"
#include <limits>

#if defined(max)
// This silly macro will prevent me to do std::numeric_limits<>::max. Good!
#pragma push_macro("max")
#undef max
#define MAX_MACRO_PUSHED
#endif


//! This enumeration is used by AbstractAlgorithm::Tick to represent the internal state and tell the outer code what to do.
enum class AlgoEvent {
    dispatched, //!< some nonces have been scheduled to be tested, everything is fine
    exhausted, //!< 4Gi nonces have been consumed, it is impossible to schedule additional work, provide a new Header() asap.
    working, //!< waiting for an async operation to complete. Call GetEvents to retrieve a list of all events we're waiting on.
    results //!< at least one algorithm iteration has completed and can be restarted... as soon as you pull out results calling GetResults.
};


/*! An algorithm can be implemented in multiple ways. Each implementation might be iterated giving different versions.
This allows quite some flexibility. The 'algorithm' was called 'algorithm family' in the first M8M architecture, it was unnecessarily complicating
things in a non-performance path.
It is strongly suggest to think at those strings like they should be valid C identifiers. */
struct AlgoIdentifier {
    std::string algorithm;
    std::string implementation;
    std::string version;
    explicit AlgoIdentifier() = default;
    AlgoIdentifier(const char *a, const char *i, const char *v) : algorithm(a), implementation(i), version(v) { }
    std::string Presentation() const { return algorithm + '.' + implementation; }
    // including the version is typically not very useful for presentation purposes as algorithm versions relate to host code, not necessarily
    // to kernel versions. Better to use algorithm signature instead!

    bool AreYou(const char *algo, const char *imp) const {
        return _stricmp(algo, algorithm.c_str()) == 0 && _stricmp(imp, implementation.c_str()) == 0; // also considered only first char case-insensitive, no point really
    }
};


/*! Because of a variety of reasons M8M had a fairly complicated algorithm management. There were 'algorithm families', 'algorithm implementations' and
implementations had a set of settings. Because a friend told me I should practice templates, I went with templates but it's obvious M8M consumes very
little CPU performance so there's no real reason to work that way.

Another thing about the previous architecture is that algorithms were supposed to be dispatched independantly across devices one step each.
This has proven to be unnecessary.

With the new design, there are no more settings objects nor 'algorithm instances': they are just an instance of some class implementing AbstractAlgorithm.

Also, they're now dumb and set up at build time with full type information. External logic gets to select settings and device to use.

The way work is dispatched is also changed but that's better discussed in a derived class. At last, as OpenCL is very convincing I have decided to drop
support for other APIs and eventually think at it again in the future: this is really AbstractCLAlgorithm. 

Note the new AbstractAlgorithm is really dumb and does not even know about hashing or anything! It just consumes its inputs when told. Input management
takes place in "dispatcher objects". \sa StopWaitDispatcher */
class AbstractAlgorithm {
public:
    const AlgoIdentifier identifier;
    const cl_context context;
    const cl_device_id device;
    const asizei hashCount;
    const asizei uintsPerHash; /*!< Mining algorithms should use the following format for the result buffer (buffer of uints):
        [0] amount of candidate nonces found, let's call it candCount.
        Candidate[candCount], where the Candidate structure is
            uint nonce;
            uint hash[uintsPerHash]
        It is strongly suggested they produce an hash out so it can be checked for validity. */

    /*! This is computed as a side-effect of PrepareKernels and not much of a performance path.
    Represents the specific algorithm-implementation and version. Computed as a side effect of PrepareKernels, which is supposed to be called by Init(). */
    aulong GetVersioningHash() const { return aiSignature; }


    /*! When initialized, algorithms can optionally provide information about what they're initializing so the user can understand what's going on.
    In that case, Init() will allocate nothing and exit early. */
    struct ConfigDesc {
        aulong hashCount;
        enum AddressSpace {
            as_device,
            as_host
        };
        struct MemDesc {
            AddressSpace memoryType;
            std::string presentation;
            auint bytes;
            explicit MemDesc() : memoryType(as_device), bytes(0) { }
        };
        std::vector<MemDesc> memUsage;
        explicit ConfigDesc() : hashCount(0) { }
    };

    /*! Performs all the heavy duty required to create the resources to run the algorithm. Returns a list of all errors encountered.
    Those are really errors, so if something non-empty is returned you should bail out.
    Call this immediately after CTOR. Must be called before Tick, GetEvents, GetResults, GetVersioningHash.
    Providing a pointer to a valid ConfigDesc object degenerates this to something akin to NOP. The call might perform different validation and
    WILL NOT initialize anything for real. In particular, it does not allocate resources. It is therefore valid to call Init multiple times with
    a ConfigDesc object, even after a successful call to Init(nullptr, ...). Nonetheless, Init(nullptr, ....) cannot be called again after one
    successful execution.
    \note The easiest way to implement this is to call DescribeResources and return before PrepareResources. */
    virtual std::vector<std::string> Init(ConfigDesc *desc, AbstractSpecialValuesProvider &specialValues, const std::string &loadPathPrefix) = 0;

    //! If this returns true you're supposed to not dispatch any more work but rather upload new hash data and restart scanning hashes from 0.
    bool Overflowing() const { return nonceBase + hashCount > std::numeric_limits<auint>::max(); };

    //! Returns true if algorithm expects block input hash in big-endian form. Dispatcher will have to pack data differently.
    virtual bool BigEndian() const = 0;

    /*! Using the provided command-queue/device assume all input buffers have been correctly setup and run a whole algorithm iteration (all involved steps).
    Compute exactly <i>amount</i> hashes, starting from hash=nonceBase.
    It is assumed count <= this->hashCount.
    \note Some kernels have requirements on workgroup size and thus put a requirement on amount being a multiple of WG size.
    Of course this base class does not care; derived classes must be careful with setup, including rebinding special resources. */
    void RunAlgorithm(cl_command_queue q, asizei amount);

    void Restart(asizei nonceStart = 0) { nonceBase = nonceStart; }

    /*! Returns a value used to compute network difficulty. This can be called by multiple threads so it must be re-entrant,
    not much of a big deal as it's usually just returning a constant. */
    virtual aulong GetDifficultyNumerator() const = 0;

protected:
    struct WorkGroupDimensionality {
        auint dimensionality;
        asizei wgs[3]; //!< short for work group size
        WorkGroupDimensionality(auint x)                   : dimensionality(1) { wgs[0] = x;    wgs[1] = 0;    wgs[2] = 0; }
        WorkGroupDimensionality(auint x, auint y)          : dimensionality(2) { wgs[0] = x;    wgs[1] = y;    wgs[2] = 0; }
        WorkGroupDimensionality(auint x, auint y, auint z) : dimensionality(3) { wgs[0] = x;    wgs[1] = y;    wgs[2] = z; }
        explicit WorkGroupDimensionality() = default;
    };
    struct KernelRequest {
        std::string fileName;
        std::string entryPoint;
        std::string compileFlags;
        WorkGroupDimensionality groupSize;
        std::string params;
    };

    struct ResourceRequest {
        std::string name;
        asizei bytes;
        cl_mem_flags memFlags;
        const aubyte *initialData; //!< must provide this->bytes amount of bytes, not owned
        bool immediate; /*!< if this is true, then this does not allocate a buffer, useful to give uints to kernels for example.
                        Immediates are not counted in memory footprint, even though they are most likely pushed to a cbuffer anyway! */
        aubyte imValue[8]; //!< used when immediate is true to store the data

        cl_image_format channels; //!< Only used if image. Parameter is considered image if imageDesc.width != 0
        cl_image_desc imageDesc;

        std::string presentationName; //!< only used when not empty, overrides name for user-presentation purposes

        bool useProvidedBuffer; //!< true if initialData is to be used from host memory directly, only relevant at buffer creation
                                //!< \note For immediates, the initialData pointer is rebased to imValue anyway so this is a bit moot.

        explicit ResourceRequest() { }
        ResourceRequest(const char *name, cl_mem_flags allocationFlags, asizei footprint, const void *initialize = nullptr) {
            this->name = name;
            memFlags = allocationFlags;
            bytes = footprint;
            initialData = reinterpret_cast<const aubyte*>(initialize);
            immediate = false;
            memset(&channels, 0, sizeof(channels));
            memset(&imageDesc, 0, sizeof(imageDesc));
            useProvidedBuffer = false;
        }
        ResourceRequest(const ResourceRequest &src) { // note: this is default copy ctor, it is fine... except not when this is an immediate
            name = src.name;    // a better way to do this would be to have a base class (?)
            bytes = src.bytes;
            memFlags = src.memFlags;
            initialData = src.initialData;
            immediate = src.immediate;
            memcpy_s(imValue, sizeof(imValue), src.imValue, sizeof(src.imValue));
            channels = src.channels;
            imageDesc = src.imageDesc;
            presentationName = src.presentationName;
            if(immediate) initialData = imValue;
        }
    };
    //! This is syntactic sugar only. It is imperative this does not add subfields as ResourceRequests are often passed around by copy.
    template<typename scalar>
    struct Immediate : ResourceRequest {
        Immediate(const char *name, const scalar &value) : ResourceRequest(name, 0, sizeof(value)) {
            immediate = true;
            memcpy_s(imValue, sizeof(imValue), &value, sizeof(value));
            initialData = imValue;
        }
    };

    /*! \param ctx OpenCL context used for creating kernels and resources. Kernels take a while to build and are very small so they can be shared
                   across devices... but they currently don't.
        \param dev This is the device this algorithm is going to use for the bulk of processing. Note complicated algos might be hybrid GPU-CPU
                   and thus require multiple devices. This extension is really only meaningful for a derived class.

        Don't mess with CL here. Actual heavy lifting in Init()! Ideally this should be NOP.
    */
    AbstractAlgorithm(asizei numHashes, cl_context ctx, cl_device_id dev, const char *algo, const char *imp, const char *ver, asizei candHashUints)
        : identifier(algo, imp, ver), context(ctx), device(dev), hashCount(numHashes), uintsPerHash(candHashUints) {
    }

    /*! Allow user to estimate memory footprint without allocating real memory. */
    std::vector<std::string> DescribeResources(ConfigDesc &desc, ResourceRequest *resources, asizei numResources, const AbstractSpecialValuesProvider &specialValues) const;

    /*! Derived classes are expected to call this somewhere in their ctor. It deals with allocating memory and eventually initializing it in a
    data-driven way. Note special resources cannot be created using this, at least in theory. Just create them in the ctor before PrepareKernels. 
    While this is allowed to throw, it is suggested to produce a list of errors to be returned by Init(). */
    std::vector<std::string> PrepareResources(ResourceRequest *resources, asizei numResources, const AbstractSpecialValuesProvider &specialValues);

    //! Similarly, kernels are described by data and built by resolving the previously declared resources. Device used to pull out eventual error logs.
    std::vector<std::string> PrepareKernels(KernelRequest *kernels, asizei numKernels, AbstractSpecialValuesProvider &specialValues, const std::string &loadPathPrefix);

    struct KernelDriver : WorkGroupDimensionality {
        cl_kernel clk;
        std::vector< std::pair<cl_uint, LateBinding> > dtBindings; /*!< dispatch time bindings. For each element,
                                                                   .first is algorithm parameter index,
                                                                   .second is *persistent* buffer where AbstractSpecialValuesProvider will push! */
        explicit KernelDriver() = default;
        KernelDriver(const WorkGroupDimensionality &wgd, cl_kernel k) : WorkGroupDimensionality(wgd) { clk = k; }
    };

    std::vector<KernelDriver> kernels;
    std::vector<ResourceRequest> resRequests;
    std::map<std::string, cl_mem> resHandles;

    /*! Late-bound buffers. This is indexed by SpecialValueBinding::index.
    Derived classes shall update this before allowing this class RunAlgorithm to run as it needs to access this to map to the correct values. */
    std::vector<cl_mem> lbBuffers;

private:
    aulong aiSignature = 0; //!< \sa GetVersioningHash()
    asizei nonceBase = 0;

    //! Called at the end of PrepareKernels. Given a cl_kernel and its originating KernelRequest object, generates a stream of clSetKernelArg according
    //! to its internal bindings, resHandles and resRequests (for immediates).
    void BindParameters(KernelDriver &kd, const KernelRequest &bindings, AbstractSpecialValuesProvider &specialValues);

    /*! Called at the end of PrepareKernels as an aid. Combines kernel file names, entrypoints, compile flags algo name and everything
    required to uniquely identify what's going to be run. */
    aulong ComputeVersionedHash(const KernelRequest *kerns, asizei numKernels, const std::map<std::string, std::string> &src) const;
};

#if defined MAX_MACRO_PUSHED
#pragma pop_macro("max")
#undef MAX_MACRO_PUSHED
#endif
