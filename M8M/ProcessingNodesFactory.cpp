/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "ProcessingNodesFactory.h"


ProcessingNodesFactory::DriverSelection ProcessingNodesFactory::NewDriver(const char *driver, const char *algo, const char *impl) {
    if(_stricmp(driver, "ocl") && _stricmp(driver, "opencl") && _stricmp(driver, "cl")) return ds_badAPI;
    if(!_stricmp(algo, "qubit")) {
        build.reset(new AlgoMiner<bv::Qubit>(sleepFunc));
        this->algo = a_qubit;
        algoName = "qubit";
    }
    else if(!_stricmp(algo, "grsmyr")) {
        build.reset(new AlgoMiner<bv::MyriadGroestl>(sleepFunc));
        this->algo = a_grsmyr;
        algoName = "grsmyr";
    }
    else if(!_stricmp(algo, "neoScrypt")) {
        build.reset(new AlgoMiner<bv::NeoScrypt<256, 32, 10, 128>>(sleepFunc));
        this->algo = a_neoscrypt;
        algoName = "neoscrypt";
    }
    else if(!_stricmp(algo, "fresh")) {
        build.reset(new AlgoMiner<bv::Fresh>(sleepFunc));
        this->algo = a_fresh;
        algoName = "fresh";
    }
    if(!build) return ds_badAlgo;
    
    switch(this->algo) {
    case a_qubit:
        if(_stricmp(impl, "fiveSteps") == 0) {
            implName = "fiveSteps";
            factory = &qfsAF;
        }
        break;
    case a_fresh:
        if(_stricmp(impl, "warm") == 0) {
            implName = "warm";
            factory = &fwAF;
        }
        break;
    case a_grsmyr:
        if(_stricmp(impl, "monolithic") == 0) {
            implName = "monolithic";
            factory = &grsmyrMonoAF;
        }
        break;
    case a_neoscrypt:
        if(_stricmp(impl, "smooth") == 0) {
            implName = "smooth";
            factory = &nssAF;
        }
        break;
    }
    if(!factory) return ds_badImpl;
    return ds_success;
}


bool ProcessingNodesFactory::AddPool(const AbstractWorkSource &pool) { 
    if(_stricmp(pool.algo.c_str(), algoName.c_str()) == 0) {
        build->RegisterWorkProvider(pool);
        return true;
    }
    return false;
}


void ProcessingNodesFactory::ExtractSelectedConfigurations(const rapidjson::Value &implParams) {
    if(implParams.IsNull()) return;
    if(implParams.IsArray()) {
        for(auto el = implParams.Begin(); el != implParams.End(); ++el) configurations.push_back(el);
        nullConfigs = false;
    }
    if(implParams.IsObject()) {
        configurations.push_back(&implParams);
        nullConfigs = false;
    }
    if(configurations.empty()) return;
    if(!factory) throw std::exception("Call NewDriver first");
    invalidReasons.resize(configurations.size());
    for(asizei i = 0; i < configurations.size(); i++) {
        auto wontdo(factory->Parse(*configurations[i]));
        if(wontdo.empty() == false) invalidReasons[i] = std::move(wontdo);
    }

}


std::unique_ptr<MinerSupport> ProcessingNodesFactory::SelectSettings(OpenCL12Wrapper &everyDevice, const OpenCL12Wrapper::ErrorFunc &errorFunc) {
    auto result(std::make_unique<MinerSupport>());
    if(!factory) return result;
    auto validConfig = [](const std::vector<std::string> &badStuff) { return badStuff.empty(); };
    if(std::count_if(invalidReasons.cbegin(), invalidReasons.cend(), validConfig) == 0) return result;

    configDesc.resize(configurations.size());
    result->niceDevices.resize(everyDevice.platforms.size());
    std::vector<std::pair<bool, cl_device_id>> linearDevices;
    for(auto &p : everyDevice.platforms) {
        for(auto &d : p.devices) linearDevices.push_back(std::make_pair(false, d.clid));
    }
    auto matchLinear = [&linearDevices](cl_device_id dev) {
        return std::find_if(linearDevices.begin(), linearDevices.end(), [&dev](const std::pair<bool, cl_device_id> &entry) { return dev == entry.second; });
    };
    result->devConfReasons.resize(linearDevices.size());

    asizei platIndex = 0;
    for(asizei platIndex = 0; platIndex < everyDevice.platforms.size(); platIndex++) {
        const auto &plat(everyDevice.platforms[platIndex]);
        std::vector<cl_device_id> nice;
        std::vector<asizei> usedConfig;
        for(asizei cfg = 0; cfg < configurations.size(); cfg++) {
            configDesc[cfg].specified = configurations[cfg];
            if(invalidReasons[cfg].size()) configDesc[cfg].rejectReasons = std::move(invalidReasons[cfg]);
            else {
                factory->Parse(*configurations[cfg]); // already tested, set config
                for(auto &dev : plat.devices) {
                    auto match(matchLinear(dev.clid));
                    if(match->first) continue;
                    match->first = true;
                    auto badDev(factory->Eligible(plat.clid, dev.clid));
                    if(badDev.empty()) {
                        nice.push_back(dev.clid);
                        usedConfig.push_back(cfg);
                        auint li = auint(matchLinear(dev.clid) - linearDevices.cbegin());
                        configDesc[cfg].devices.push_back(li);
                        linearIndex.insert(std::make_pair(dev.clid, li));
                    }
                    else {
                        auto &errSlot(result->devConfReasons[matchLinear(dev.clid) - linearDevices.cbegin()]);
                        errSlot.push_back(MinerSupport::ConfReasons());
                        errSlot.back().configIndex = cfg;
                        errSlot.back().bad = std::move(badDev);
                    }
                }
            }
        }
        if(nice.size()) {
            result->niceDevices[platIndex].ctx = MakeContext(plat.clid, nice, result->niceDevices.data() + platIndex, errorFunc);
            result->niceDevices[platIndex].devices.resize(nice.size());
            for(asizei cp = 0; cp < nice.size(); cp++) {
                result->niceDevices[platIndex].devices[cp] = { matchLinear(nice[cp]) - linearDevices.cbegin(), nice[cp], usedConfig[cp] };
            }
        }
        platIndex++;
    }
    return result;
}


void ProcessingNodesFactory::BuildAlgos(std::vector< std::unique_ptr<AbstractAlgorithm> > &algos, const MinerSupport::CooperatingDevices &group) {
    if(group.devices.empty()) return;
    for(auto &dev : group.devices) {
        factory->Parse(*configurations[dev.configIndex]);
        algos.push_back(std::move(factory->New(group.ctx, dev.clid)));
        std::unique_ptr<StopWaitDispatcher> disp(std::make_unique<StopWaitDispatcher>(*algos.back()));
        build->AddDispatcher(disp);
    }
}


void ProcessingNodesFactory::DescribeConfigs(commands::monitor::ConfigInfoCMD::ConfigDesc &result, asizei devCount, const std::vector< std::unique_ptr<AbstractAlgorithm> > &algos) {
    result.selected = factory != nullptr;
    result.specified = !nullConfigs;
    result.configs = std::move(configDesc);
    result.informative.resize(devCount);
    for(asizei i = 0; i < algos.size(); i++) {
        auto slot = linearIndex.find(algos[i]->device);
        if(slot == linearIndex.cend()) throw std::exception("Could not reconstruct device->linearIndex, this should be impossible!");
        result.informative[slot->second] = std::move(algoDescriptions[i]);
    }
}



cl_context ProcessingNodesFactory::MakeContext(cl_platform_id plat, const std::vector<cl_device_id> &eligible, MinerSupport::CooperatingDevices *mark, const OpenCL12Wrapper::ErrorFunc &errorFunc) {
    cl_context_properties cprops[] = {
        CL_CONTEXT_PLATFORM, cl_context_properties(plat), 0
    };
    cl_int error;
    cl_context ctx = clCreateContext(cprops, cl_uint(eligible.size()), eligible.data(), errorFunc, mark, &error);
    if(error != CL_SUCCESS) throw "Error creating context: " + std::to_string(error);
    return ctx;
}
