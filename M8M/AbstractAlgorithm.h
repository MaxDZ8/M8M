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


struct SignedAlgoIdentifier : AlgoIdentifier {
    aulong signature = 0;
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
    virtual SignedAlgoIdentifier Identify() const = 0;

    const cl_context context;
    const cl_device_id device;
#if defined REPLICATE_CLDEVICE_LINEARINDEX // ugly hack to support non-unique cl_device_id values
    cl_uint linearDeviceIndex = auint(-1);
#endif
    const asizei hashCount;
    const asizei uintsPerHash; /*!< Mining algorithms should use the following format for the result buffer (buffer of uints):
        [0] amount of candidate nonces found, let's call it candCount.
        Candidate[candCount], where the Candidate structure is
            uint nonce;
            uint hash[uintsPerHash]
        It is strongly suggested they produce an hash out so it can be checked for validity. */

    //! Implementations don't load from disk anymore. Instead, they request an handle to a persistent, RO buffer of data.
    //! Try to make this non-throwing, add error descriptions to the vector instead.
    typedef std::function<std::pair<const char*, asizei>(std::vector<std::string> &errors, const std::string &kernelFile)> SourceCodeBufferGetterFunc;

    // Algorithms are no more created here. Instead, given a configuration we pull resource and kernel descriptions.
    // A data-driven algorithm is then built elsewhere.
    struct ResourceRequest {
        std::string name;
        asizei bytes = 0;
        cl_mem_flags memFlags = 0;
        const aubyte *initialData = nullptr; //!< must provide this->bytes amount of bytes, not owned
        bool immediate = false; /*!< if this is true, then this does not allocate a buffer, useful to give uints to kernels for example.
                        Immediates are not counted in memory footprint, even though they are most likely pushed to a cbuffer anyway! */
        aubyte imValue[8]; //!< used when immediate is true to store the data

        cl_image_format channels; //!< Only used if image. Parameter is considered image if imageDesc.width != 0
        cl_image_desc imageDesc;

        std::string presentationName; //!< only used when not empty, overrides name for user-presentation purposes

        bool useProvidedBuffer = false; //!< true if initialData is to be used from host memory directly, only relevant at buffer creation
                                //!< \note For immediates, the initialData pointer is rebased to imValue anyway so this is a bit moot.

        explicit ResourceRequest() {
            memset(&channels, 0, sizeof(channels));
            memset(&imageDesc, 0, sizeof(imageDesc));
        }
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

    //! If this returns true you're supposed to not dispatch any more work but rather upload new hash data and restart scanning hashes from 0.
    bool Overflowing() const { return nonceBase + hashCount > std::numeric_limits<auint>::max(); };

    /*! Using the provided command-queue/device assume all input buffers have been correctly setup and run a whole algorithm iteration (all involved steps).
    Compute exactly <i>amount</i> hashes, starting from hash=nonceBase.
    It is assumed count <= this->hashCount.
    \note Some kernels have requirements on workgroup size and thus put a requirement on amount being a multiple of WG size.
    Of course this base class does not care; derived classes must be careful with setup, including rebinding special resources. */
    void RunAlgorithm(cl_command_queue q, asizei amount);

    void Restart(asizei nonceStart = 0) { nonceBase = nonceStart; }

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
    static void DescribeResources(std::vector<ConfigDesc::MemDesc> &desc, const std::vector<ResourceRequest> &res);

protected:
    /*! \param ctx OpenCL context used for creating kernels and resources. Kernels take a while to build and are very small so they can be shared
                   across devices... but they currently don't.
        \param dev This is the device this algorithm is going to use for the bulk of processing. Note complicated algos might be hybrid GPU-CPU
                   and thus require multiple devices. This extension is really only meaningful for a derived class.

        Don't mess with CL here. Actual heavy lifting in Init()! Ideally this should be NOP.
    */
    AbstractAlgorithm(asizei numHashes, cl_context ctx, cl_device_id dev, asizei candHashUints)
        : context(ctx), device(dev), hashCount(numHashes), uintsPerHash(candHashUints) {
    }

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
    asizei nonceBase = 0;
};

#if defined MAX_MACRO_PUSHED
#pragma pop_macro("max")
#undef MAX_MACRO_PUSHED
#endif
