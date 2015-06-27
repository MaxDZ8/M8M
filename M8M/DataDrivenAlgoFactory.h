/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "clAlgoFactories.h"
#include <vector>
#include <memory>

/*! Since algorithms are now built by ResourceRequest and KernelRequest arrays, we must provide a way to mangle settings and produce those arrays accordingly.
The DataDrivenAlgoFactory is therefore misnomed as it really creates those arrays, which the DataDrivenAlgorithm then mangles, self-building itself. */
struct DataDrivenAlgoFactory : AbstractAlgoFactory {
    /*! When a candidate hash is found, its originating nonce is put in the result buffer.
    Following the nonce, the algorithm will append this amount of uints being the hash to match.
    \todo could I just obtain that by inference from the verifier or something? This is really binary in nature. */
    auint candHashUints = 0;

    /*! LinearIntensity setting determines hashCount by multiplying its value by this. */
    auint intensityScale = 0;

    /*! How to determine buffer sizes? They can be constant or they can be multiplicative to hash count.
    In the latter case, the name-value correspondance is kept there. */
    std::vector<std::pair<std::string, auint>> linearSize;

    void DeclareResource(std::string &name, const rapidjson::Value &desc, KnownConstantProvider &cryptoConstants) {
        if(ParseSpecial(desc, name, cryptoConstants)) return;
        if(ParseImmediate(desc, name)) return;
        if(desc.IsArray() == false) throw std::string("Algorithm resource descriptor \"") + name + "\" must be an array.";
        ParseCustom(desc, name);
    }

    void DeclareKernel(const rapidjson::Value &stage) {
        AbstractAlgorithm::KernelRequest gen;
        auto mkString = [](const rapidjson::Value &str) { return std::string(str.GetString(), str.GetStringLength()); };
        gen.fileName = mkString(stage[0u]);
        gen.entryPoint = mkString(stage[1]);
        gen.compileFlags = mkString(stage[2]);
        gen.groupSize = ParseGroupSize(stage[3]);
        gen.params = mkString(stage[4]);
        kernels.push_back(gen);
    }

    //! AbstractAlgoFactory - - - - - - - - - - - - - - - - - - - - - -
    void Resources(std::vector<AbstractAlgorithm::ResourceRequest> &res, KnownConstantProvider &K) const;
    void Kernels(std::vector<AbstractAlgorithm::KernelRequest> &kern) const {
        auto copy(kernels);
        kern = std::move(kernels);
    }
    asizei GetHashCount() const { return linearIntensity * intensityScale; }
    asizei GetNumUintsPerCandidate() const { return candHashUints; }
    // SignedAlgoIdentifier GetAlgoIdentifier() const = 0;
    asizei GetIntensityMultiplier() const { return intensityScale; }
    asizei GetBiggestBufferSize(asizei hashCount) const {
        asizei ret = 0;
        for(const auto &res : resources) {
            asizei size;
            if(res.linearIndex) size = linearSize[res.bytes].second * hashCount;
            else size = res.bytes;
            ret = size > ret? size : ret;
        }
        return ret;
    }

private:
    struct MetaResource : AbstractAlgorithm::ResourceRequest {
        bool linearIndex = false; //!< When this is true, the byte footprint is not real but rather the index of the hashCount multiplier to use.
    };
    std::vector<MetaResource> resources;
    std::vector<std::pair<asizei, std::unique_ptr<aubyte[]>>> persistentBlobs;
    std::vector<AbstractAlgorithm::KernelRequest> kernels;

    bool ParseSpecial(const rapidjson::Value &arr, std::string &name, KnownConstantProvider &ck);
    static cl_mem_flags ParseMemFlags(const rapidjson::Value &string);
    bool ParseImmediate(const rapidjson::Value &arr, std::string &name);
    void ParseValue(aubyte *dst, asizei rem, const std::string &type, const rapidjson::Value &im, const std::string &name);
    void ParseCustom(const rapidjson::Value &arr, std::string &name);
    static AbstractAlgorithm::WorkGroupDimensionality ParseGroupSize(const rapidjson::Value &arr);
};
