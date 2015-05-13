/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <functional>
#include "AlgoMiner.h"
#include "../BlockVerifiers/bv/Fresh.h"
#include "../BlockVerifiers/bv/MyriadGroestl.h"
#include "../BlockVerifiers/bv/Qubit.h"
#include "../BlockVerifiers/bv/NeoScrypt.h"
#include "clAlgoFactories.h"
#include "MiningPerformanceWatcher.h"
#include "OpenCL12Wrapper.h"
#include <algorithm>
#include <set>
#include "commands/Monitor/ConfigInfoCMD.h"
#include "../Common/AbstractWorkSource.h"


/*! Those structures hold important stuff the miner (an object implementing NonceFindersInterface) needs to work.
They have to exist as long as the miner exists. This object also defines a destruction order so stuff won't go awry.
Those objects own everything they contain so they have pure move semantics. */
struct MinerSupport {
    struct ConfReasons {
        asizei configIndex = asizei(-1);
        std::vector<std::string> bad;
    };
    std::vector< std::vector<MinerSupport::ConfReasons> > devConfReasons; //!< in linear device order

    struct CooperatingDevices {
        cl_context ctx = 0; //!< one context from one platform including all eligible devices for at least one setting
        struct Device {
            auint linearIndex;
            cl_device_id clid;
            asizei configIndex;
        };
        std::vector<Device> devices; //!< linear index of device, can be empty --> platform unused. linear index and handle
    };
    std::vector<CooperatingDevices> niceDevices;
    std::vector<std::unique_ptr<AbstractAlgorithm>> algo;
    MinerSupport(const MinerSupport &other) = delete;
    MinerSupport& operator=(const MinerSupport &other) = delete;
    explicit MinerSupport() = default;
    ~MinerSupport() {
        for(auto &el : niceDevices) if(el.ctx) clReleaseContext(el.ctx);
    }
};


/*! Instantiating processing nodes is a bit complicated.
There was this function, InstanceProcessingNodes taking 5 parameters, not all quite easy to explain.
Just better to have an object instead so I can better explain lifetimes and all. */
class ProcessingNodesFactory {
public:
    ProcessingNodesFactory(std::function<void(auint)> sleepms) : sleepFunc(sleepms), driver(d_opencl), algo(a_null) { }

    enum DriverSelection {
        ds_success,
        ds_badAPI,
        ds_badAlgo,
        ds_badImpl
    };

    /*! The first step in building a miner is to select a the algorithm to mine. For the time being, only a single implementation can be used.
    This really does nothing to the hardware but rather selects the hash verifier to use. */
    DriverSelection NewDriver(const char *driver, const char *algo, const char *impl);

    /*! Add all the pools. Returns true if the pool provides compatible work and is thus added to the list of pools of the nonce finders. */
    bool AddPool(const AbstractWorkSource &pool);

    //! Then pass the "implParams" object pulled from config file. This will setup the list of "selected" configurations.
    void ExtractSelectedConfigurations(const rapidjson::Value &implParams);

    //! Assign valid selected configurations to devices and build context structures across eligible devices from the same platform.
    std::unique_ptr<MinerSupport> SelectSettings(OpenCL12Wrapper &everyDevice, const OpenCL12Wrapper::ErrorFunc &errorFunc);

    //! Call this multiple times to build the various mining algorithms which are also added to the NonceFindersInterface.
    void BuildAlgos(std::vector< std::unique_ptr<AbstractAlgorithm> > &algos, const MinerSupport::CooperatingDevices &group);

    /*! When completed, just pull back result and keep it around as you need it. This object can be destroyed. */
    std::unique_ptr<NonceFindersInterface> Finished(const std::string &loadPath, AbstractNonceFindersBuild::PerformanceMonitoringFunc performance) {
        buildErrors = std::move(build->Init(loadPath, &algoDescriptions));
        if(buildErrors.size()) {
            build.reset();
            return std::move(build);
        }
        build->onIterationCompleted = performance;
        build->linearDevice = linearIndex; // don't move it, also needed for DescribeConfigs
        build->Start();
        return std::move(build);
    }

    //! Call this function to construct configuration information as required by ConfigInfoCMD.
    //! Note this is truly valid only if Finished returned a valid object. Otherwise, the results might be slightly inconsistent but hopefully still helpful.
    void DescribeConfigs(commands::monitor::ConfigInfoCMD::ConfigDesc &result, asizei totalDeviceCount, const std::vector< std::unique_ptr<AbstractAlgorithm> > &algos);

    //! This is a bit ugly. Ok, it is very ugly. The bottom line is that pools right now require algorithm information to be built as they need to build the
    //! work factory to produce headers to hash... so we need to pull out this accordingly. Quite ugly!
    static std::vector<AbstractWorkSource::AlgoInfo> GetAlgoInformations();

private:
    enum Driver {
        d_null,
        d_opencl
    } driver;
    enum Algo {
        a_null,
        a_qubit,
        a_fresh,
        a_grsmyr,
        a_neoscrypt
    } algo;
    std::string algoName, implName;
    bool nullConfigs = true;
    std::vector<const rapidjson::Value*> configurations;
    std::vector< std::vector<std::string> > invalidReasons; //!< if [i] is not empty config is discarded, otherwise, it might be used or not.
    std::map<cl_device_id, asizei> linearIndex;

    std::vector<std::string> buildErrors; //!< errors returned by build->Init
    std::vector<AbstractAlgorithm::ConfigDesc> algoDescriptions; 
    std::vector<commands::monitor::ConfigInfoCMD::ConfigInfo> configDesc;

    std::function<void(auint)> sleepFunc;
    std::unique_ptr<AbstractNonceFindersBuild> build;
    QubitFiveStepsAF qfsAF;
    NeoscryptSmoothAF nssAF;
    MYRGRSMonolithicAF grsmyrMonoAF;
    FreshWarmAF fwAF;
    AbstractAlgoFactory *factory = nullptr;

    /*! Which devices should mangle the selected algorithm? Each device might come in its own implementation and setting.
    This does not take care of mapping devices to settings to algorithms. Instead, some outer component just tells us to take ownership
    of a dispatcher. Note instead the algorithms used by the dispatchers are not owned by the dispatcher and must be kept around somewhere.
    This also instructs the object being built with the device linear index.
    \sa GetAlgoFactory */
    void AddDispatcher(std::unique_ptr<StopWaitDispatcher> &dispatcher, asizei deviceIndex) {
        build->linearDevice.insert(std::make_pair(dispatcher->algo.device, deviceIndex));
        ScopedFuncCall clearNew([this, &dispatcher]() { build->linearDevice.erase(dispatcher->algo.device); });
        build->AddDispatcher(dispatcher);
        clearNew.Dont();
    }

    static cl_context MakeContext(cl_platform_id plat, const std::vector<cl_device_id> &eligible, MinerSupport::CooperatingDevices *mark, const OpenCL12Wrapper::ErrorFunc &errorFunc);
};
