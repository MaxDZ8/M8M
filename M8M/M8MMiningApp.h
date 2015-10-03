/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "M8MPoolMonitoringApp.h"
#include "AlgoMiner.h"
#include <CL/cl.h>
#include "KnownHardware.h"
#include "clAlgoFactories.h"
#include "commands/Monitor/SystemInfoCMD.h"
#include "commands/Monitor/AlgosCMD.h"
#include "commands/Monitor/ConfigInfoCMD.h"
#include "AlgoSourcesLoader.h"


/*! Building the miner is a fairly involved process which requires to deal with some CL details as well as the high-level configuration issues.
Remember that besides that, M8M also needs to track quite some information to present the user in its UI.
So, this really does two things (more like one and half) by building some bookkeeping information as byproduct.
It also includes most details of the old "OpenCL12Wrapper" object, which I never truly liked but the more specific scope also allows me to
integrate several additional information which was previously tracked by other means such as the configuration used by each device. */
class M8MMiningApp : public M8MPoolMonitoringApp,
                     protected commands::monitor::SystemInfoCMD::ProcessingNodesEnumeratorInterface,
                     protected commands::monitor::ConfigInfoCMD::ConfigDescriptorInterface {
public:
    M8MMiningApp(NetworkInterface &factory) : M8MPoolMonitoringApp(factory) { }

    /*! Pull up kernel descriptions from file. This loads up the sources to be later used to initialize the threads.
    It also computes signatures for all algorithms, regardless they're used or not. */
    void LoadKernelDescriptions(const std::wstring &desc, const std::string &kernPathPrefix) { sources.Load(desc, kernPathPrefix); }

    /*! Estabilishes a consistent order across computing devices reported by the CL platforms, whatever they're used or not.
    This is important for UI mostly but also comes useful internally to avoid having pointers around. */
    void EnumerateDevices();

    /*! The big deal. I could break this in multiple external steps but I'm not really sure it's a good idea.
    After all, temporary information should not go out of its scope in theory.
    \returns Number of queues successfully created. */
    asizei StartMining(const std::string &algo, const rapidjson::Value &allConfigs);

    void Refresh(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);

    ~M8MMiningApp() {
        for(auto &el : computeNodes) {
            // clReleaseDevice NOP for real devices!
            if(el.ctx) clReleaseContext(el.ctx);
        }
    }
    
    CanonicalInfo GetCanonicalAlgoInfo(const std::string &algo) const {
        for(asizei test = 0; test < sources.GetNumAlgos(); test++) {
            if(_stricmp(algo.c_str(), sources.GetAlgoName(test).c_str()) == 0) return sources.GetCanon(test);
        }
        throw "GetCanonicalALgoInfo(" + algo + "), failed to match.";
    }

private:
    bool validConfigSelected = false;
    std::string miningAlgorithm;
    std::unique_ptr<NonceFindersInterface> miner;
    struct Device {
        cl_device_id clid = 0;
        auint linearIndex = 0;
        asizei configIndex = asizei(-1);
        AbstractAlgorithm::ConfigDesc resources;

        KnownHardware::Architecture architecture = KnownHardware::arch_unknown;
        struct Type {
            bool cpu, gpu, accelerator, /*custom,*/ defaultDevice;
            Type() { cpu = gpu = accelerator = defaultDevice = false; }
        } type;
    };
    struct Platform {
        cl_platform_id clid = 0;
        /*! this context contains all the activated devices by this platform. Note it is not owned by this structure.
        If no devices are activated, it is left 0. */
        cl_context ctx = 0;
        std::vector<Device> devices;
	};
    std::vector<Platform> computeNodes;
    std::chrono::system_clock::time_point firstNonce; //!< if clear, not found yet.

    struct DevRequirements {
        std::vector<std::string> Eligible(cl_platform_id pid, cl_device_id did) { return std::vector<std::string>(); }
    };

    struct ConfigHolder { //!< holds reference to the algo-impl configuration and list of reject reasons, if any, also resources consumed
        const rapidjson::Value *value;
        std::vector<std::string> staticRejects;
        std::string impl;
        // resources are not kept here. They are kept in mapped device instead so each device, even using the same config can have different resources.
        // This is especially important for "AUTO" targeting in
    };
    std::vector<ConfigHolder> configData;

    std::vector<const rapidjson::Value*> Deduce(const rapidjson::Value &algoSettings);
    //! This should have better naming as it also builds the miner... but I cannot figure out anything more meaningful.
    //! In theory building the miner should go somewhere else but it's handy to have it there to reduce the amount of repetitions.
    std::pair<const char*, AbstractAlgoFactory*> NewAlgoFactory(std::unique_ptr<AbstractNonceFindersBuild> &miner, const char *algo, const char *impl);
    void SelectSettings(const std::vector<std::pair<const char*, AbstractAlgoFactory*>> &factories,
                        const std::vector<const rapidjson::Value*> &configs,
                        const std::vector<bool> &valid); //!< mangle settings and assign them to devices
    asizei BuildEveryUsefulContext();
    void ParseRequirements(DevRequirements &build, const rapidjson::Value &match) const;

    void WorkChange(const AbstractWorkSource &source, std::unique_ptr<stratum::AbstractWorkFactory> &recent) {
        if(miner) miner->SetWorkFactory(source, recent);
    }
    void DiffChange(const AbstractWorkSource &source, const stratum::WorkDiff &recent) {
        if(miner) miner->SetDifficulty(source, recent);
    }

    static void _stdcall ErrorsToSTDOUT(const char *err, const void *priv, size_t privSz, void *userData) {
        using std::cout;
        using std::endl;
	    cout<<"ERROR reported \""<<err<<"\"";
	    if(priv && privSz) {
		    cout<<endl;
		    const char *oct = reinterpret_cast<const char*>(priv);
		    for(asizei loop = 0; loop < privSz; loop++) cout<<oct[loop];
	    }
	    cout<<endl;
	    cout<<"userdata is "<<(userData? "non-" : "")<<"null";
	    cout<<endl;
	    cout.flush();
    } //!< \todo not sure what I should do with that. Certainly not just this. This will need me to either get creative or use a global...

    KnownConstantProvider cryptoConstants;

protected:
    AlgoSourcesLoader sources;

    /*! Why is the given device not mapped to to the given config? Again, temporary objects here. */
    virtual void AddDeviceReject(asizei config, std::string &utf8, asizei devIndex) = 0;

    /*! Performance monitoring callback, called asynchronously by the miner thread(s). */
    virtual void IterationCompleted(asizei devIndex, bool found, std::chrono::microseconds elapsed) = 0;

    /*! Stuff returned from a mining device. Validated but potentially stale. Not sent to pool yet! */
    virtual void UpdateDeviceStats(const VerifiedNonces &found) = 0;

    asizei GetNumDevices() const {
        asizei count = 0;
        for(const auto &plat : computeNodes) count += plat.devices.size();
        return count;
    }

    // commands::monitor::SystemInfoCMD::ProcessingNodesEnumeratorInterface /////// ugly ////////////////////
    const char* GetAPIName() const { return "OpenCL 1.2"; }
    asizei GetNumPlatforms() const { return computeNodes.size(); }
    std::string GetPlatformString(asizei p, PlatformInfoString pis) const;
    asizei GetNumDevices(asizei p) const { return computeNodes[p].devices.size(); }
	std::string GetDeviceInfo(asizei plat, asizei dev, DeviceInfoString prop) const;
	auint GetDeviceInfo(asizei plat, asizei dev, DeviceInfoUint prop);
	aulong GetDeviceInfo(asizei plat, asizei dev, DeviceInfoUnsignedLong prop);
	bool GetDeviceInfo(asizei plat, asizei dev, DeviceInfoBool prop);
	LDSType GetDeviceLDSType(asizei plat, asizei dev);
    DevType GetDeviceType(asizei plat, asizei dev);

    // commands::monitor::ConfigInfoCMD::ConfigDescriptorInterface //////////////////////////////////////////
    bool ValidSelection() const { return validConfigSelected; }
    std::string GetSelectedAlgorithm() const { return miningAlgorithm; }
    bool ValidConfig() const { return configData.size() != 0; }
    asizei GetNumDeducedConfigs() const { return configData.size(); }
    commands::monitor::ConfigInfoCMD::ConfigInfo GetConfig(asizei i) const;
    bool GetResources(AbstractAlgorithm::ConfigDesc &desc, auint dev) const;
};
