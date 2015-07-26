/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "AlgoSourcesLoader.h"


void AlgoSourcesLoader::Load(const std::wstring &algoDescFile, const std::string &kernLoadPath) {
    using namespace rapidjson;
    FILE *jsonFile = nullptr;
    if(_wfopen_s(&jsonFile, algoDescFile.c_str(), L"rb")) return;
    ScopedFuncCall autoClose([jsonFile]() { fclose(jsonFile); });

    char jsonReadBuffer[512];
    FileReadStream jsonIN(jsonFile, jsonReadBuffer, sizeof(jsonReadBuffer));
    AutoUTFInputStream<unsigned __int32, FileReadStream> input(jsonIN);
    Document algos;
    algos.ParseStream< 0, AutoUTF<unsigned> >(input);
    if(algos.HasParseError()) throw std::exception("Invalid algorithm description file!");
    // ^ That's not really supposed to happen. Really. Just do your homework or leave it alone.
    std::vector<std::string> uniqueFiles;
    Value *verification = nullptr;
    for(auto algo = algos.MemberBegin(); algo != algos.MemberEnd(); ++algo) {
        for(auto impl = algo->value.MemberBegin(); impl != algo->value.MemberEnd(); ++impl) {
            const std::string implName(impl->name.GetString(), impl->name.GetStringLength());
            if(implName == "$verification") {
                verification = &impl->value;
                continue;
            }
            auto kernels = impl->value.FindMember("kernels");
            if(kernels == impl->value.MemberEnd()) throw std::exception("Missing required kernel sequence");
            if(kernels->value.IsArray() == false) throw std::exception("Kernels specifications must be an array");
            if(kernels->value.Size() == 0) throw std::exception("Kernels specifications array is empty");
            for(auto entry = kernels->value.Begin(); entry != kernels->value.End(); ++entry) ValidateExtractFile(uniqueFiles, *entry);

            // Also take the chance for some other validation.
            auto ver = impl->value.FindMember("version");
            if(ver == impl->value.MemberEnd()) throw std::exception("Missing algorithm-implementation versioning string");
            if(ver->value.IsString() == false) throw std::exception("Algorithm-implementation versioning must be a string");
            std::string verString(ver->value.GetString(), ver->value.GetStringLength());
            if(verString.empty()) throw std::exception("Algorithm-implementation versioning string is empty!");
        }
        if(!verification) {
            const std::string name(algo->value.GetString(), algo->value.GetStringLength());
            throw "Algorithm " + name + " is missing nonce validator.";
        }
    }

    // Building the algorithms. For the time being, this happens as usual.
    std::vector< std::future< std::vector<char> > > loading(uniqueFiles.size());
    auto loadSource = [](std::string &filename) -> std::vector<char> {
        std::ifstream disk(filename, std::ios::binary);
        if(disk.is_open() == false) throw std::string("Could not open \"") + filename + '"';
        disk.seekg(0, std::ios::end);
        auto size = disk.tellg();
        if(size >= 1024 * 1024 * 8) throw std::string("Kernel source in \"") + filename + "\" is too big, measures " + std::to_string(size) + " bytes!";
        std::vector<char> source(asizei(size) + 1);
        disk.seekg(0, std::ios::beg);
        disk.read(source.data(), size);
        source[asizei(size)] = 0; // not required by specification, but some older drivers are stupid
        return source;
    };
    for(asizei slot = 0; slot < uniqueFiles.size(); slot++) {
        std::string src(kernLoadPath + uniqueFiles[slot]);
        loading[slot] = std::move(std::async(loadSource, src));
    }
    for(asizei loop = 0; loop < loading.size(); loop++) {
        LoadedSource add;
        try {
            auto finished = std::move(loading[loop].get());
            add.source = std::move(finished);
            loadedSources.insert(std::make_pair(std::move(uniqueFiles[loop]), std::move(add)));
        } catch(std::string &err) {
            add.errors.push_back(err);
            loadedSources.insert(std::make_pair(std::move(uniqueFiles[loop]), std::move(add)));
        } catch(std::exception &err) {
            add.errors.push_back(err.what());
            loadedSources.insert(std::make_pair(std::move(uniqueFiles[loop]), std::move(add)));
        }
    }
    // Errors are signaled only when buffers are requested. That's a bit backwards but that's the best I can think about.

    // Now let's build the algorithm-implementation data.
    for(auto algo = algos.MemberBegin(); algo != algos.MemberEnd(); ++algo) {
        const std::string algorithm(algo->name.GetString(), algo->name.GetStringLength());
        auto container = std::find_if(chains.begin(), chains.end(), [&algorithm](const std::unique_ptr<AlgoFamily> &test) {
            return _stricmp(test->name.c_str(), algorithm.c_str()) == 0;
        });
        if(container == chains.end()) {
            auto temp(algorithm);
            chains.push_back(std::make_unique<AlgoFamily>());
            container = chains.begin() + (chains.size() - 1);
            container->get()->name = std::move(temp);
        }
        if(container->get()->name != algorithm) {
            throw std::string("Algorithm \"") + container->get()->name + "\" aliasing with \"" + algorithm + "\", not allowed to happen.";
        }
        for(auto impl = algo->value.MemberBegin(); impl != algo->value.MemberEnd(); ++impl) {
            std::string implName(impl->name.GetString(), impl->name.GetStringLength());
            if(implName == "$verification") {
                container->get()->verifier.reset(NewVerifier(impl->value));
                continue;
            }
            auto kernels = impl->value.FindMember("kernels");
            auto ver = impl->value.FindMember("version");
            AlgoIdentifier id;
            id.algorithm = algorithm;
            id.implementation = std::move(implName);
            id.version = std::string(ver->value.GetString(), ver->value.GetStringLength());
            auto signature = ComputeVersionedHash(id, kernels->value);
            auto &implArray(container->get()->impl);
            auto uniqueness = std::find_if(implArray.cbegin(), implArray.cend(), [&id](const std::unique_ptr<AlgoFamily::Implementation> &test) {
                return test->name == id.implementation;
            });
            if(uniqueness != implArray.cend()) throw algorithm + "." + id.implementation + " already declared.";
            auto add(std::make_unique<AlgoFamily::Implementation>());
            add->version = std::move(id.version);
            add->signature = signature;
            add->name = std::move(id.implementation);
            add->algo = &container->get()->name;
            add->name = add->name;

            auto hashUints = impl->value.FindMember("candHashUints");
            if(hashUints != impl->value.MemberEnd()) {
                if(hashUints->value.IsUint()) add->candHashUints = hashUints->value.GetUint();
                else throw std::exception("\"candHashUints\" must be unsigned integer.");
            }
            auto intScale = impl->value.FindMember("intensityScaling");
            if(intScale == impl->value.MemberEnd()) throw std::exception("\"intensityScaling\" missing, it is required.");
            if(intScale->value.IsUint() == false) throw std::exception("\"intensityScaling\" must be uint.");
            add->intensityScale = intScale->value.GetUint();

            auto linearSizes = impl->value.FindMember("linearSizes");
            if(linearSizes != impl->value.MemberEnd()) {
                if(linearSizes->value.IsObject()) {
                    for(auto lin = linearSizes->value.MemberBegin(); lin != linearSizes->value.MemberEnd(); ++lin) {
                        const std::string name(lin->name.GetString(), lin->name.GetStringLength());
                        if(lin->value.IsUint() == false) throw std::string("\"linearSizes.") + name + "\" must be an uint!";
                        add->linearSize.push_back(std::make_pair(name, lin->value.GetUint()));
                    }
                }
                else throw std::exception("\"linearSizes\" must be an object.");
            }

            auto resources = impl->value.FindMember("resources");
            if(resources != impl->value.MemberEnd()) {
                if(resources->value.IsObject() == false) throw std::exception("\"resources\" must be an object");
                for(auto res = resources->value.MemberBegin(); res != resources->value.MemberEnd(); ++res) {
                    std::string name(res->name.GetString(), res->name.GetStringLength());
                    add->DeclareResource(name, res->value, cryptoConstants);
                }
            }
            for(auto kern = kernels->value.Begin(); kern != kernels->value.End(); ++kern) add->DeclareKernel(*kern);
            implArray.push_back(std::move(add));
        }
    }
}


void AlgoSourcesLoader::ValidateExtractFile(std::vector<std::string> &uniques, const rapidjson::Value &entry) {
    // It really validates the format only, not the contents.
    if(entry.IsArray() == false) throw std::exception("Kernel stage is not an array");
    if(entry.Size() != 5) throw std::exception("Kernel stage array must count 5 elements.");
    if(entry[0u].IsString() == false) throw std::exception("filename must be string.");
    if(entry[1].IsString() == false) throw std::exception("kernel entry point must be a string.");
    if(entry[2].IsString() == false) throw std::exception("compile flags must be a string.");
    if(entry[3].IsArray() == false) throw std::exception("Work size must be an array.");
    if(entry[3].Size() < 1 || entry[3].Size() > 3) throw std::exception("Work size must be 1D, 2D or 3D.");
    for(rapidjson::SizeType check = 0; check < entry[3].Size(); check++) {
        if(entry[3][check].IsUint() == false) throw std::exception("Work size must uints.");
    }
    if(entry[4].IsString() == false) throw std::string("Kernel parameter bindings must be a string.");
    std::string filename(entry[0u].GetString(), entry[0u].GetStringLength());
    auto unique = std::find(uniques.cbegin(), uniques.cend(), filename);
    if(unique == uniques.cend()) uniques.push_back(std::move(filename));
}


std::pair<const char*, asizei> AlgoSourcesLoader::GetSourceBuffer(std::vector<std::string> &errors, const std::string &kernFile) const {
    auto got = loadedSources.find(kernFile);
    if(got == loadedSources.cend()) return std::make_pair(nullptr, 0);
    if(got->second.errors.size()) {
        for(const auto &el : got->second.errors) errors.push_back(el);
        return std::make_pair(nullptr, 0);
    }
    return std::make_pair(got->second.source.data(), got->second.source.size());
}


aulong AlgoSourcesLoader::ComputeVersionedHash(const AlgoIdentifier &identifier, const rapidjson::Value &kerns) const {
    std::string sign(identifier.algorithm + '.' + identifier.implementation + '.' + identifier.version + '\n');
    auto mkString = [](const rapidjson::Value &strval) { return std::string(strval.GetString(), strval.GetStringLength()); };
    for(rapidjson::SizeType step = 0; step < kerns.Size(); step++) {
        auto filename(mkString(kerns[step][0u]));
        sign += ">>>>" + filename + ':' + mkString(kerns[step][1]) + '(' + mkString(kerns[step][2]) + ')' + '\n';
        // groupSize is most likely not to be put there...
        // are param bindings to be put there?
        std::vector<std::string> errors;
        auto src(GetSourceBuffer(errors, filename));
        if(errors.size() || src.first == nullptr) continue;
        std::string source(src.first, src.second);
        sign += source + "<<<<\n";
    }
    hashing::SHA256 blah(reinterpret_cast<const aubyte*>(sign.c_str()), sign.length());
    hashing::SHA256::Digest blobby;
    blah.GetHash(blobby);
    aulong ret = 0; // ignore endianess here so we get to know host endianess by algo signature
    for(asizei loop = 0; loop < blobby.size(); loop += 8) {
        aulong temp;
        memcpy_s(&temp, sizeof(temp), blobby.data() + loop, sizeof(temp));
        ret ^= temp;
    }
    return ret;
}


BlockVerifierInterface* AlgoSourcesLoader::NewVerifier(const rapidjson::Value &desc) {
    if(desc.IsArray() == false) throw std::exception("$implementation must be array");
    if(desc.Size() < 1) throw std::exception("\"$implementation\" must count at least one element.");
    auto build(std::make_unique<ModularBlockVerifier>());
    struct Head {
        typedef StagePair RetType;
        const char *name;
        std::function<RetType()> generate;
    };
    std::vector<Head> heads {
        { "luffa512",   []() { return MakePair<HLuffa512>(); } },
        { "shavite512", []() { return MakePair<HShaVite512>(); } },
        { "groestl512", []() { return MakePair<HGroestl512>(); } },
        { "neoscrypt",  []() { return MakePair<NeoScrypt<256, 32, 10, 128>>(); } }
    };

    struct Intermediate {
        typedef IntermediateHasherInterface* RetType;
        const char *name;
        std::function<RetType()> generate;
    };
    std::vector<Intermediate> later {
        { "cubehash512",  []() { return new CubeHash512; } },
        { "shavite512",   []() { return new ShaVite512; } },
        { "SIMD512",      []() { return new SIMD512; } },
        { "ECHO512",      []() { return new ECHO512; } },
        { "sha256_trunc", []() { return new SHA256_trunc; } }
    };
    TruncateAdapter truncateAdapter;
    std::vector<AbstractAdapter*> adapters {
        &truncateAdapter
    };

    StagePair gen;
    if(desc[0u].IsString()) gen = NewStage(std::string(desc[0u].GetString(), desc[0u].GetStringLength()), heads);
    else if(desc[0u].IsObject()) gen = NewStage(desc[0u], heads, adapters, desc.Size() == 1);
    else throw std::exception("Head of block verifier must be string or object.");

    build->head = gen.header;
    build->delHead = gen.same == false;
    build->chained.push_back(std::move(std::unique_ptr<IntermediateHasherInterface>(gen.intermediate)));

    for(rapidjson::SizeType loop = 1; loop < desc.Size(); loop++) {
        asizei isize = build->chained[loop - 1]->GetHashByteCount();
        std::unique_ptr<IntermediateHasherInterface> add;
        if(desc[loop].IsString()) add.reset(NewStage(std::string(desc[loop].GetString(), desc[loop].GetStringLength()), later));
        else if(desc[loop].IsObject()) add.reset(NewStage(desc[loop], later, adapters, loop == desc.Size() - 1));
        else throw std::exception("Chained hash must be a string.");
        if(!add->CanMangle(isize)) throw "Error in defined block verifier, previous stage outputs " + std::to_string(isize) + " bytes, not supported.";
        build->chained.push_back(std::move(add));
    }
    if(build->chained.back()->GetHashByteCount() != 32) throw std::exception("Final hash stage does not produce 32 bytes, not supported.");
    return build.release();
}
