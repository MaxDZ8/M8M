/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/encodedstream.h>
#include <rapidjson/error/en.h>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <future>
#include "AlgoImplUserTracker.h"
#include "commands/Monitor/AlgosCMD.h"
#include "DataDrivenAlgoFactory.h"
#include "../BlockVerifiers/BlockVerifierInterface.h"
#include "../BlockVerifiers/HashBlocks.h"
#include "../BlockVerifiers/SHA256_trunc.h"
#include "../BlockVerifiers/NeoScrypt.h"


class AlgoSourcesLoader : public AlgoImplUserTracker, public commands::monitor::AlgosCMD::AlgoEnumeratorInterface {
public:
    /*! Loading takes place synchronously by parsing a json file describing the various algorithms to load.
    Those imply a set of files to load. They are loaded in persistent memory buffers which are flushed when Flush() is called.
    You're supposed to keep those around as long as at least one thread is initializing. */
    void Load(const std::wstring &algoDescFile, const std::string &kernLoadPrefix);

    //! Get the rid of source code buffers. From now on, requesting source will be always result in an error.
    //! this (object) is still useful to keep around as it keeps the algorithm signatures for polling.
    void Flush() { loadedSources.clear(); }
    bool Flushed() const { return loadedSources.size() == 0; } //!< of course only makes sense after Load has been called.

    std::pair<const char*, asizei> GetSourceBuffer(std::vector<std::string> &errors, const std::string &kernFile) const;

    const char* GetPersistentImplName(asizei algo, asizei impl) const {
        return chains[algo]->impl[impl]->name.c_str();
    }
    BlockVerifierInterface* GetVerifier(asizei algo) const {
        return chains[algo]->verifier.get();
    }
    AbstractAlgoFactory* GetFactory(asizei algo, asizei impl) const {
        return chains[algo]->impl[impl].get();
    }

    // AlgoEnumeratorInterface -------------------------------------------------------------
    asizei GetNumAlgos() const { return chains.size(); }
    std::string GetAlgoName(asizei algo) const { return chains[algo]->name; }
    asizei GetNumImplementations(asizei algo) const { return chains[algo]->impl.size(); }
    void GetAlgoDescription(commands::monitor::AlgosCMD::AlgoDesc &desc, asizei algo, asizei impl) const {
        const auto &src(*chains[algo]->impl[impl]);
        desc.implementation = src.name;
        desc.signature = src.signature;
        desc.version = src.version;
    }

private:
    struct LoadedSource {
        std::vector<char> source;
        std::vector<std::string> errors;
    };

    std::map<std::string, LoadedSource> loadedSources;

    struct AlgoFamily {
        std::string name;
        struct Implementation : DataDrivenAlgoFactory {
            std::string name, version;
            aulong signature = 0;
            const std::string *algo = nullptr;
            SignedAlgoIdentifier GetAlgoIdentifier() const {
                SignedAlgoIdentifier ret;
                ret.algorithm = *algo;
                ret.implementation = name;
                ret.version = version;
                ret.signature = signature;
                return ret;
            }
        };
        std::vector< std::unique_ptr<Implementation> > impl;
        std::unique_ptr<BlockVerifierInterface> verifier;
    };
    std::vector< std::unique_ptr<AlgoFamily> > chains;
    KnownConstantProvider cryptoConstants;

    struct EasyGoingDDAlgoFactory : DataDrivenAlgoFactory {
        SignedAlgoIdentifier id;
    };

    //! Given an entry in the algo-impl "kernels" array, this validates the format and pulls out the filename, which is added to the list
    //! of files to load, if unique.
    static void ValidateExtractFile(std::vector<std::string> &uniques, const rapidjson::Value &entry);

    aulong ComputeVersionedHash(const AlgoIdentifier &desc, const rapidjson::Value &kernArray) const;
    static BlockVerifierInterface* NewVerifier(const rapidjson::Value &desc);

    //! A block verifier build by interpreting data. It could be called DataDrivenBlockVerifier but I'm using a different nomenclature to avoid confusion.
    struct ModularBlockVerifier : BlockVerifierInterface {
        AbstractHeaderHasher *head = nullptr; //!< points to chained[0], or something inside it.
        bool delHead = false;
        std::vector<std::unique_ptr<IntermediateHasherInterface>> chained;
        std::array<aubyte, 32> Hash(std::array<aubyte, 80> baseBlockHeader, auint nonce) {
            std::vector<aubyte> around(head->GetHeader(baseBlockHeader, nonce));
            std::vector<aubyte> output;
            for(auto &chain : chained) {
                chain->Hash(output, around);
                around = std::move(output);
            }
            std::array<aubyte, 32> temp;
            for(asizei cp = 0; cp < temp.size(); cp++) temp[cp] = around[cp];
            return temp;
        }
        ~ModularBlockVerifier() { if(delHead && head) delete head; }
    };

    template<typename Leaf>
    static typename Leaf::RetType NewStage(const std::string &entry, const std::vector<Leaf> &generators) {
        for(auto &test : generators) {
            if(_stricmp(test.name, entry.c_str()) == 0) return test.generate();
        }
        throw "Unknown hasher \"" + entry + '"';
    }

    /*! This is basically a std::pair<AbstractHeaderHasher*, IntermediateHasherInterface*> but there's a quirk.
    In line of concept the pair was introduced to handle different syntax by maintaining type safety BUT
    it has no way to know if the two pointers go to the same object... and I don't like casting.
    So, this struct keeps track of whatever the pointers go to the same objects or not. */
    struct StagePair {
        explicit StagePair() { }
        StagePair(AbstractHeaderHasher* h, IntermediateHasherInterface* i) : intermediate(i), header(h), same(true) { }
        IntermediateHasherInterface *intermediate = nullptr;
        AbstractHeaderHasher *header = nullptr;
        bool same = true;
    };

    struct AbstractAdapter {
        const char *name;

        /*! \param [in] own Leaf hasher generated by previously parsing the "hash" key. On call, nobody owns that so the first thing is to take ownership.
        You're supposed to put it right away in the generated adapter, which is returned.
        \param [in] obj Object generating the adapter, the "op" has been parsed already and matches this while "hash" has been parsed producing own.
        \param [in] last true if this is the last stage in a chain. Some parameters might be omitted in that case. */
        virtual IntermediateHasherInterface* Generate(IntermediateHasherInterface *own, const rapidjson::Value &obj, bool last) = 0;

        StagePair Generate(StagePair own, const rapidjson::Value &obj, bool last) {
            std::unique_ptr<AbstractHeaderHasher> guard;
            if(own.same == false && own.header) guard.reset(own.header);
            own.intermediate = Generate(own.intermediate, obj, last);
            own.same = false;
            guard.release();
            return own;
        }
    };

    struct TruncateAdapter : AbstractAdapter, IntermediateHasherInterface {
        explicit TruncateAdapter() { name = "truncate"; }
        IntermediateHasherInterface* Generate(IntermediateHasherInterface *own, const rapidjson::Value &obj, bool last) {
            std::unique_ptr<IntermediateHasherInterface> guard(own);
            auto add(std::make_unique<TruncateAdapter>());
            auto outlen(obj.FindMember("size"));
            add->size = 32;
            if(outlen == obj.MemberEnd()) {
                if(!last) throw "Validator stage \"truncate\" missing output length, required to be there for non-last stages.";
            }
            else if(outlen->value.IsUint() == false) throw "Validator stage \"truncate\", key \"size\" must be uint.";
            else add->size = outlen->value.GetUint();
            add->hasher = std::move(guard);
            return add.release();
        }
        asizei size = 0;
        std::unique_ptr<IntermediateHasherInterface> hasher;


        std::vector<aubyte>& Hash(std::vector<aubyte> &hash, const std::vector<aubyte> &input) {
            hasher->Hash(hash, input);
            hash.resize(size);
            return hash;
        }
        bool CanMangle(asizei inputByteCount) const { return hasher->CanMangle(inputByteCount); }
        asizei GetHashByteCount() const { return size; }
    };

    template<typename Leaf>
    static typename Leaf::RetType NewStage(const rapidjson::Value &obj, const std::vector<Leaf> &generators, const std::vector<AbstractAdapter*> &adapters, bool last) {
        auto opKey = obj.FindMember("op");
        if(opKey == obj.MemberEnd()) throw std::exception("Validator stage missing \"op\"");
        if(opKey->value.IsString() == false)  throw std::exception("Validator stage \"op\" key must have string value");
        const std::string op(opKey->value.GetString(), opKey->value.GetStringLength());
        for(auto &test : adapters) {
            if(_stricmp(test->name, op.c_str()) == 0) {
                auto hash(obj.FindMember("hash"));
                Leaf::RetType real;
                if(hash == obj.MemberEnd()) throw "Validator stage \"truncate\" missing \"hash\" key.";
                else if(hash->value.IsString()) real = NewStage(std::string(hash->value.GetString(), hash->value.GetStringLength()), generators);
                else if(hash->value.IsObject()) real = NewStage(hash->value, generators, adapters, last);
                else throw "Validator stage got invalid \"hash\" value.";
                return test->Generate(real, obj, last);
            }
        }
        throw "Unknown hasher adapter \"" + op + '"';
    }

    template<typename Type>
    static StagePair MakePair() {
        Type *gen = new Type;
        return StagePair(gen, gen);
    }
};
