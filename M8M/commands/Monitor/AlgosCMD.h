/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCommand.h"
#include "AbstractAlgorithm.h"
#include <array>


namespace commands {
namespace monitor {


class AlgosCMD : public AbstractCommand {
public:
    struct AlgoDesc {
        std::string implementation;
        std::string version;
        aulong signature;
    };

    //! \todo to be improved in the future, I want to inform the user of all available algos.
    struct AlgoEnumeratorInterface {
        virtual asizei GetNumAlgos() const = 0;
        virtual std::string GetAlgoName(asizei algo) const = 0;
        virtual asizei GetNumImplementations(asizei algo) const = 0;
        virtual void GetAlgoDescription(AlgoDesc &desc, asizei algo, asizei impl) const = 0;
    };
    AlgosCMD(AlgoEnumeratorInterface &algos) : all(algos), AbstractCommand("algo") { }

	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		using namespace rapidjson;
		build.SetObject();
        auto &alloc(build.GetAllocator());
        for(asizei algo = 0; algo < all.GetNumAlgos(); algo++) {
            auto aname(all.GetAlgoName(algo));
            if(all.GetNumImplementations(algo) == 0) continue; // not likely to happen anyway
            Value arr(rapidjson::kArrayType);
            for(asizei impl = 0; impl < all.GetNumImplementations(algo); impl++) {
                Value triplet(rapidjson::kArrayType);
                AlgoDesc desc;
                all.GetAlgoDescription(desc, algo, impl);
                auto temp(Hex(desc.signature));
                Value implName(desc.implementation.c_str(), SizeType(desc.implementation.length()), alloc);
                Value version(desc.version.c_str(), SizeType(desc.version.length()), alloc);
                Value signature(temp.c_str(), SizeType(temp.length()), alloc);
                Value entry(kArrayType);
                entry.PushBack(implName, alloc);
                entry.PushBack(version, alloc);
                entry.PushBack(signature, alloc);
                arr.PushBack(entry, alloc);
            }
            Value adda(rapidjson::kObjectType);
            adda.AddMember(Value(aname.c_str(), SizeType(aname.length()), alloc), arr, alloc);
        }
		return nullptr;
	}
private:
    AlgoEnumeratorInterface &all;
    static std::string Hex(aulong signature) {
		char buffer[20]; // 8*2+1 would be sufficient for aulong
		_ui64toa_s(signature, buffer, sizeof(buffer), 16);
        return std::string(buffer);
    }
};


}
}
