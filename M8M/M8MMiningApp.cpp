/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "M8MMiningApp.h"


void M8MMiningApp::EnumerateDevices() {
    const asizei count = 8;
    cl_platform_id store[count];
    cl_uint avail = count;
    cl_int err = clGetPlatformIDs(count, store, &avail);
    const asizei numPlatforms(avail);
    if(err != CL_SUCCESS) throw std::exception("Failed to build OpenCL platforms list.");
    computeNodes.resize(numPlatforms);
    std::vector<cl_device_id> devBuff(8);
    asizei linearIndex = 0;
    for(asizei loop = 0; loop < numPlatforms; loop++) {
        computeNodes[loop].clid = store[loop];
        asizei tried = 0;
        while(tried < 5) {
            tried++;
            err = clGetDeviceIDs(store[loop], CL_DEVICE_TYPE_ALL, cl_uint(devBuff.size()), devBuff.data(), &avail);
            if(avail > devBuff.size()) {
                devBuff.resize(avail);
                continue;
            }
            /* custom devices? */
            for(asizei dev = 0; dev < avail; dev++) {
                Device::Type type;
                cl_device_type clType;
                err = clGetDeviceInfo(devBuff[dev], CL_DEVICE_TYPE, sizeof(clType), &clType, NULL);
                if(err != CL_SUCCESS) throw std::exception("This was supposed to never happen, system must be in bad state!");
                type.cpu = (clType & CL_DEVICE_TYPE_CPU) != 0;
                type.gpu = (clType & CL_DEVICE_TYPE_GPU) != 0;
                type.accelerator = (clType & CL_DEVICE_TYPE_ACCELERATOR) != 0;
                type.defaultDevice = (clType & CL_DEVICE_TYPE_DEFAULT) != 0;
                computeNodes[loop].devices.push_back(Device());
                computeNodes[loop].devices.back().clid = devBuff[dev];
                computeNodes[loop].devices.back().type = type;
                computeNodes[loop].devices.back().linearIndex = auint(linearIndex++);
            }
            break;
        }
        if(tried == 5) throw std::exception("Something fishy is going on with device enumeration!");
    }
#if defined REPLICATE_CLDEVICE_LINEARINDEX
    asizei match = REPLICATE_CLDEVICE_LINEARINDEX;
    asizei li = 0;
    for(asizei loop = 0; loop < computeNodes.size(); loop++) li += computeNodes[loop].devices.size();
    for(asizei loop = 0; loop < computeNodes.size(); loop++) {
        if(match < computeNodes[loop].devices.size()) {
            computeNodes[loop].devices.push_back(computeNodes[loop].devices[match]);
            computeNodes[loop].devices.back().linearIndex = auint(li++);
            break;
        }
        match -= computeNodes[loop].devices.size();
    }
#endif
}


//! \todo This takes for granted driver is opencl... not that this is a real problem as different drivers would likely result
//! in a different object being used... anyway...
asizei M8MMiningApp::StartMining(const std::string &algo, const rapidjson::Value &everything) {
    if(everything.IsObject() == false) {
        Error(L"Algo configs must be an object.");
        return 0;
    }
    // Look it up case-insensitive.
    const rapidjson::Value *useful = nullptr;
    for(auto el = everything.MemberBegin(); el != everything.MemberEnd(); ++el) {
        if(algo.size() != el->name.GetStringLength()) continue;
        if(_strnicmp(el->name.GetString(), algo.c_str(), algo.size()) == 0) {
            useful = &el->value;
            break;
        }
    }
    if(useful == nullptr) {
        const std::string meh("Missing configs for the algo \"" + algo + "\".");
        std::wstring unicode;
        unicode.reserve(meh.length());
        for(asizei cp = 0; cp < meh.size(); cp++) unicode.push_back(meh[cp]);
        Error(unicode);
        return 0;
    }
    validConfigSelected = true;
    auto implConfigs(Deduce(*useful));
    std::vector<bool> validImpl(implConfigs.size());
    for(auto &el : validImpl) el = true;
    if(implConfigs.empty()) {
        Error(L"No configs for the current algo.");
        return 0;
    }
    configData.resize(implConfigs.size());
    for(asizei cp = 0; cp < implConfigs.size(); cp++) configData[cp].value = implConfigs[cp];
    std::vector<std::pair<const char*, AbstractAlgoFactory*>> factories; // map is nicer but case insensivity is meh, factories are owned by the source loader
    std::unique_ptr<AbstractNonceFindersBuild> miner;
    for(asizei loop = 0; loop < implConfigs.size(); loop++) {
        auto entry(implConfigs[loop]->FindMember("impl"));
        if(entry == implConfigs[loop]->MemberEnd()) {
            configData[loop].staticRejects.push_back(std::string("Missing \"impl\" value."));
            validImpl[loop] = false;
            continue;
        }
        const std::string impl(entry->value.GetString(), entry->value.GetStringLength());
        configData[loop].impl = impl;
        auto gen(NewAlgoFactory(miner, algo.c_str(), impl.c_str()));
        if(gen.first == nullptr || gen.second == nullptr) {
            configData[loop].staticRejects.push_back("Unknown implementation \"" + impl + "\".");
            validImpl[loop] = false;
            continue;
        }
        auto errors(gen.second->Parse(*implConfigs[loop]));
        if(errors.size()) {
            for(auto &err : errors) configData[loop].staticRejects.push_back(err);
            validImpl[loop] = false;
            continue;
        }
        auto unique(std::find_if(factories.cbegin(), factories.cend(), [&impl](const std::pair<const char*, AbstractAlgoFactory*> &check) {
            return _stricmp(check.first, impl.c_str()) == 0;
        }));
        if(unique != factories.cend()) continue;
        factories.push_back(std::make_pair(gen.first, gen.second));
    }
    SelectSettings(factories, implConfigs, validImpl);
    if(!BuildEveryUsefulContext()) {
        Error(L"No devices eligible to processing.");
        return 0;
    }
    for(const auto &plat : computeNodes) { // we could limit this to only used devices or only used by this miner, but handy to have
        for(const auto &dev : plat.devices) miner->linearDevice.insert(std::make_pair(dev.clid, dev.linearIndex));
    }
    miner->onIterationCompleted = [this](asizei devIndex, bool found, std::chrono::microseconds elapsed) {
        IterationCompleted(devIndex, found, elapsed);
    };
    // Before creating the miners let's register the pools. It could be done anywhere but I like to validate some configuration first.
    for(asizei loop = 0; loop < GetNumServers(); loop++) miner->RegisterWorkProvider(GetPool(loop));
    // Ok, now we're ready. Almost. I will now have to iterate the devices and configs once again.
    // Yes, I take it easy. It's a fast operation anyway, how many devices can you have?
    auto lambda = [this](std::vector<std::string> &errors, const std::string &kernFile) -> std::pair<const char*, asizei> {
        return sources.GetSourceBuffer(errors, kernFile);
    };
    std::function<std::pair<const char*, asizei>(std::vector<std::string> &errors, const std::string &kernFile)> loader(lambda);
    asizei launched = 0;
    for(auto &plat : computeNodes) {
        for(auto &dev : plat.devices) {
            if(dev.configIndex == asizei(-1)) continue;
            auto &impl(implConfigs[dev.configIndex]->FindMember("impl")->value);
            const std::string use(impl.GetString(), impl.GetStringLength());
            AbstractAlgoFactory *factory = nullptr;
            for(asizei loop = 0; loop < factories.size(); loop++) {
                if(_stricmp(factories[loop].first, use.c_str()) == 0) {
                    factory = factories[loop].second; // guaranteed to happen because of construction
                    break;
                }
            }
            factory->Parse(*implConfigs[dev.configIndex]);
            // Eligibility already evaluated. Note only Parse sets internal state, Eligible does not!
            AbstractNonceFindersBuild::AlgoBuild build;
            factory->Kernels(build.kern);
            factory->Resources(build.res, cryptoConstants);
            sources.AddUser(algo, use);
            dev.resources.hashCount = factory->GetHashCount();
            build.numHashes = factory->GetHashCount();
            build.candHashUints = factory->GetNumUintsPerCandidate();
            build.ctx = plat.ctx;
            build.dev = dev.clid;
            build.identifier = factory->GetAlgoIdentifier();
            AbstractAlgorithm::DescribeResources(dev.resources.memUsage, build.res);
            // At this point we used to init this work queue. This is now just matters of adding an entry and spawning a thread.
            const std::string algoFamily(factory->GetAlgoIdentifier().algorithm);
            miner->GenQueue(std::move(build), loader, GetCanonicalAlgoInfo(algoFamily)
#if defined REPLICATE_CLDEVICE_LINEARINDEX
                , dev.linearIndex
#endif
            );
            launched++;
        }
    }
    this->miner = std::move(miner);
    miningAlgorithm = algo;
    return launched;
}


void M8MMiningApp::Refresh(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
    NonceOriginIdentifier from;
    VerifiedNonces sharesFound;
    using namespace std::chrono;
    static system_clock::time_point nextStatusCheck;
    if(miner) {
        if(miner->ResultsFound(from, sharesFound)) {
            if(firstNonce == system_clock::time_point()) {
                firstNonce = system_clock::now();
                std::wstring msg(L"Found my first result!\n");
                if(sharesFound.wrong) msg = L"GPU produced bad numbers.\nSomething is very wrong!"; //!< \todo blink yellow for a few seconds every time a result is wrong
                else msg += L"Numbers are getting crunched as expected.";
                Popup(msg.c_str());
                ChangeState(sharesFound.wrong? STATE_ERROR : STATE_OK, true);
            }
            else if(this->GetNumActiveServers()) { // As long as mining is going on and producing results, I consider it a win, provided stuff can go somewhere!
                //! \todo Also consider the amount of rejects - how to? With multiple pools it's not so easy.
                //! \todo Also consider the amount of HW errors. This is easier than pools but I still have to think about it.
                if(GetIconState() != STATE_OK) ChangeState(STATE_OK, true);
            }
            UpdateDeviceStats(sharesFound); // this one goes to a derived class
            SendResults(from, sharesFound); // this to a base class
        }

        if(nextStatusCheck == system_clock::time_point()) nextStatusCheck = system_clock::now() + minutes(1);
        else if(nextStatusCheck < system_clock::now()) {
            nextStatusCheck += minutes(1);
            auto status(miner->GetNumWorkQueues());
            if(status[0] == status[1]) Error(L"All miners failed!");
            else if(status[0]) Error(std::to_wstring(status[0]) + L"miner" + (status[0] > 1? L"s" : L"") + L" failed!");
            else {
                asizei slow = 0;
                for(asizei loop = 0; loop < status[1]; loop++) {
                    auto probe(miner->GetTerminationReason(loop));
                    if(std::get<1>(probe) == miner->s_created) continue; // not very likely considering a result has been already found by somebody else
                    auto lastWU(miner->GetLastWUGenTime(loop));
                    if(lastWU + minutes(5) < std::chrono::system_clock::now()) slow++;
                    // In theory this should be a function of block time so for BTC we need at least 10 minutess to force a change by changing block.
                    // Everybody else use shorter blocks (BSTY being a notable exception) we should be rolling work anyway if we run out of nonce2 bits.
                    // Soooo... this is cutting it short. But I don't care.
                }
                if(slow) Error(L"Some miners are not generating any work!");
            }
        }
    }
    if(sources.Flushed() == false) {
        if(miner) {
            using namespace std::chrono;
            static const seconds pollInterval(30);
            static system_clock::time_point lastCheck;
            if(lastCheck == system_clock::time_point()) lastCheck = system_clock::now();
            else if(system_clock::now() >= lastCheck + pollInterval) {
                for(asizei check = 0; check < miner->GetNumWorkQueues()[1]; check++) {
                    auto stat(miner->GetTerminationReason(check));
                    if(std::get<1>(stat) != miner->s_created) sources.Initialized(check);
                }
                lastCheck = system_clock::now();
            }
        }
        if(sources.GetNumRegisteredConsumers() == sources.GetNumInitializedConsumers() || !miner) sources.Flush();
    }
    M8MPoolMonitoringApp::Refresh(toRead, toWrite);
}


std::vector<const rapidjson::Value*> M8MMiningApp::Deduce(const rapidjson::Value &algoSettings) {
    std::vector<const rapidjson::Value*> ret;
    if(algoSettings.IsObject()) ret.push_back(&algoSettings);
    else if(algoSettings.IsArray()) {
        for(auto el = algoSettings.Begin(); el != algoSettings.End(); ++el) {
            if(el->IsObject()) ret.push_back(el); // ignore the others, no need to be nitpicky and useful as comments
        }
    }
    return ret;
}


std::pair<const char*, AbstractAlgoFactory*> M8MMiningApp::NewAlgoFactory(std::unique_ptr<AbstractNonceFindersBuild> &miner, const char *algo, const char *impl) {
    for(asizei loop = 0; loop < sources.GetNumAlgos(); loop++) {
        const auto aname(sources.GetAlgoName(loop));
        if(_stricmp(aname.c_str(), algo)) continue;
        for(asizei inner = 0; inner < sources.GetNumImplementations(loop); inner++) {
            const char *persistent = sources.GetPersistentImplName(loop, inner);
            if(_stricmp(persistent, impl) == 0) {
                miner = std::make_unique<AlgoMiner>(*sources.GetVerifier(loop));
                return std::make_pair(persistent, sources.GetFactory(loop, inner));
            }
        }
    }
    throw std::string("Algorithm implementation \"") + algo + '.' + impl + "\" not found.";
}



void M8MMiningApp::SelectSettings(const std::vector<std::pair<const char*, AbstractAlgoFactory*>> &factories,
                                  const std::vector<const rapidjson::Value*> &configs,
                                  const std::vector<bool> &valid) {
    std::vector<DevRequirements> requirements(configs.size());
    for(asizei loop = 0; loop < configs.size(); loop++) {
        auto reqd(configs[loop]->FindMember("requirements"));
        if(reqd != configs[loop]->MemberEnd() && reqd->value.IsObject()) ParseRequirements(requirements[loop], *configs[loop]);
    }
    for(asizei loop = 0; loop < configs.size(); loop++) {
        if(valid[loop] == false) continue;
        auto specify(configs[loop]->FindMember("impl"));
        const std::string impl(specify->value.GetString(), specify->value.GetStringLength());
        auto uses(std::find_if(factories.cbegin(), factories.cend(), [&impl](const std::pair<const char*, AbstractAlgoFactory*> &check) {
            return _stricmp(impl.c_str(), check.first) == 0;
        }));
        uses->second->Parse(*configs[loop]); // initialize state
        for(auto &p : computeNodes) {
            for(auto &d : p.devices) {
                if(d.configIndex != asizei(-1)) {
                    AddDeviceReject(loop, "Already mapped to config[" + std::to_string(d.configIndex) + ']', d.linearIndex);
                    continue;
                }
                auto errors(uses->second->Eligible(p.clid, d.clid)); // fine because of construction
                bool good = errors.empty();
                for(auto &err : errors) AddDeviceReject(loop, err, d.linearIndex);
                errors = requirements[loop].Eligible(p.clid, d.clid);
                good &= errors.empty();
                for(auto &err : errors) AddDeviceReject(loop, err, d.linearIndex);
                if(good) d.configIndex = loop;
            }
        }
    }
}


asizei M8MMiningApp::BuildEveryUsefulContext() {
    asizei activate = 0;
    for(auto &plat : computeNodes) {
        std::vector<cl_device_id> eligible;
        for(auto &dev : plat.devices) {
            if(dev.configIndex != asizei(-1)) eligible.push_back(dev.clid);
        }
        if(eligible.size()) {
            cl_context_properties cprops[] = {
                CL_CONTEXT_PLATFORM, cl_context_properties(plat.clid), 0
            };
            cl_int error;
            plat.ctx = clCreateContext(cprops, cl_uint(eligible.size()), eligible.data(), ErrorsToSTDOUT, &plat, &error);
            if(error != CL_SUCCESS) throw "Error creating context: " + std::to_string(error);
        }
        activate += eligible.size();
    }
    return activate;
}

void M8MMiningApp::ParseRequirements(DevRequirements &build, const rapidjson::Value &match) const {
}

std::string M8MMiningApp::GetPlatformString(asizei p, PlatformInfoString prop) const {
    std::vector<char> text;
    asizei avail = 64;
    text.resize(avail);
    cl_platform_info clProp;
    switch(prop) {
    case pis_profile:    clProp = CL_PLATFORM_PROFILE; break;
    case pis_version:    clProp = CL_PLATFORM_VERSION; break;
    case pis_name:       clProp = CL_PLATFORM_NAME; break;
    case pis_vendor:     clProp = CL_PLATFORM_VENDOR; break;
    case pis_extensions: clProp = CL_PLATFORM_EXTENSIONS; break;
    default: throw std::exception("Impossible. Code out of sync?");
    }
    const auto &plat(computeNodes[p]);
    cl_int err = clGetPlatformInfo(plat.clid, clProp, avail, text.data(), &avail);
    if(err == CL_INVALID_VALUE) {
        text.resize(avail);
        err = clGetPlatformInfo(plat.clid, clProp, avail, text.data(), &avail);
        if(err != CL_SUCCESS) throw std::exception("Something went very wrong with CL platform properties.");
    }
    return std::string(text.data());

}

std::string M8MMiningApp::GetDeviceInfo(asizei plat, asizei devIndex, DeviceInfoString prop) const {
    asizei avail;
    cl_device_info clProp = 0;
    switch(prop) {
    case dis_chip:          clProp = CL_DEVICE_NAME; break;
    case dis_vendor:        clProp = CL_DEVICE_VENDOR; break;
    case dis_driverVersion: clProp = CL_DRIVER_VERSION; break;
    case dis_profile:       clProp = CL_DEVICE_PROFILE; break;
    case dis_apiVersion:    clProp = CL_DEVICE_VERSION; break;
    case dis_extensions:    clProp = CL_DEVICE_EXTENSIONS; break;
    default: throw std::exception("Impossible. Code out of sync?");
    }
    const auto &dev(computeNodes[plat].devices[devIndex]);
    cl_int err = clGetDeviceInfo(dev.clid, clProp, 0, NULL, &avail);
    std::vector<char> text(avail);
    err = clGetDeviceInfo(dev.clid, clProp, avail, text.data(), &avail);
    if(err != CL_SUCCESS) throw std::exception("Something went very wrong with CL platform properties.");
    return std::string(text.data());
}


auint M8MMiningApp::GetDeviceInfo(asizei plat, asizei devIndex, DeviceInfoUint prop) {
    cl_uint value;
    cl_device_info clProp = 0;
    switch(prop) {
    case diu_vendorID:  clProp = CL_DEVICE_VENDOR_ID; break;
    case diu_clusters:  clProp = CL_DEVICE_MAX_COMPUTE_UNITS; break;
    case diu_coreClock: clProp = CL_DEVICE_MAX_CLOCK_FREQUENCY; break;
    default: throw std::exception("Impossible. Code out of sync?");
    }
    const auto &dev(computeNodes[plat].devices[devIndex]);
    cl_int err = clGetDeviceInfo(dev.clid, clProp, sizeof(value), &value, NULL);
    if(err != CL_SUCCESS) throw std::exception("Something went wrong while probing device info.");
    return value;
}


aulong M8MMiningApp::GetDeviceInfo(asizei plat, asizei devIndex, DeviceInfoUnsignedLong prop) {
    cl_long value;
    cl_device_info clProp = 0;
    switch(prop) {
    case diul_maxMemAlloc:         clProp = CL_DEVICE_MAX_MEM_ALLOC_SIZE; break;
    case diul_globalMemBytes:      clProp = CL_DEVICE_GLOBAL_MEM_SIZE; break;
    case diul_ldsBytes:            clProp = CL_DEVICE_LOCAL_MEM_SIZE; break;
    case diul_globalMemCacheBytes: clProp = CL_DEVICE_GLOBAL_MEM_CACHE_SIZE; break;
    case diul_cbufferBytes:        clProp = CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE; break;
    default: throw std::exception("Impossible. Code out of sync?");
    }
    const auto &dev(computeNodes[plat].devices[devIndex]);
    cl_int err = clGetDeviceInfo(dev.clid, clProp, sizeof(value), &value, NULL);
    if(err != CL_SUCCESS) throw std::exception("Something went wrong while probing device info.");
    return value;
}


bool M8MMiningApp::GetDeviceInfo(asizei plat, asizei devIndex, DeviceInfoBool prop) {
    cl_bool value;
    cl_device_info clProp = 0;
    switch(prop) {
    case dib_ecc:          clProp = CL_DEVICE_ERROR_CORRECTION_SUPPORT; break;
    case dib_huma:         clProp = CL_DEVICE_HOST_UNIFIED_MEMORY; break;
    case dib_littleEndian: clProp = CL_DEVICE_ENDIAN_LITTLE; break;
    case dib_available:    clProp = CL_DEVICE_AVAILABLE; break;
    case dib_compiler:     clProp = CL_DEVICE_COMPILER_AVAILABLE; break;
    case dib_linker:       clProp = CL_DEVICE_LINKER_AVAILABLE; break;
    default: throw std::exception("Impossible. Code out of sync?");
    }
    const auto &dev(computeNodes[plat].devices[devIndex]);
    cl_int err = clGetDeviceInfo(dev.clid, clProp, sizeof(value), &value, NULL);
    if(err != CL_SUCCESS) throw std::exception("Something went wrong while probing device info.");
    return value != 0;
}


commands::monitor::SystemInfoCMD::ProcessingNodesEnumeratorInterface::LDSType M8MMiningApp::GetDeviceLDSType(asizei plat, asizei devIndex) {
    const auto &dev(computeNodes[plat].devices[devIndex]);
    cl_device_local_mem_type value;
    cl_int err = clGetDeviceInfo(dev.clid, CL_DEVICE_LOCAL_MEM_TYPE, sizeof(value), &value, NULL);
    if(err != CL_SUCCESS) throw std::exception("Something went wrong while probing device info.");
    if(value == CL_LOCAL) return ldsType_dedicated;
    return value == CL_GLOBAL? ldsType_global : ldsType_none;
}


commands::monitor::SystemInfoCMD::ProcessingNodesEnumeratorInterface::DevType M8MMiningApp::GetDeviceType(asizei plat, asizei devIndex) {
    DevType ret;
    const auto &dev(computeNodes[plat].devices[devIndex]);
    ret.cpu = dev.type.cpu;
    ret.gpu = dev.type.gpu;
    ret.accelerator = dev.type.accelerator;
    ret.defaultDevice = dev.type.defaultDevice;
    return ret;
}


commands::monitor::ConfigInfoCMD::ConfigInfo M8MMiningApp::GetConfig(asizei i) const {
    commands::monitor::ConfigInfoCMD::ConfigInfo ret;
    ret.specified = configData[i].value;
    ret.rejectReasons = configData[i].staticRejects;
    ret.impl = configData[i].impl;
    for(const auto &plat : computeNodes) {
        for(const auto &dev : plat.devices) {
            if(dev.configIndex == i) ret.devices.push_back(auint(dev.linearIndex));
        }
    }
    return ret;
}


bool M8MMiningApp::GetResources(AbstractAlgorithm::ConfigDesc &desc, auint devIndex) const {
    for(const auto &plat : computeNodes) {
        for(const auto &dev : plat.devices) {
            if(devIndex == dev.linearIndex) {
                desc = dev.resources;
                return true;
            }
        }
    }
    return false;
}
