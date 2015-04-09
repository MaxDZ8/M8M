/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCommand.h"
#include "AbstractAlgorithm.h"

namespace commands {
namespace monitor {

class AlgoCMD : public AbstractCommand {
    const std::string algorithm;
    const std::string implementation;
    const std::string versionHash;

    static std::string Hex(aulong signature) {
		char buffer[20]; // 8*2+1 would be sufficient for aulong
		_ui64toa_s(signature, buffer, sizeof(buffer), 16);
        return std::string(buffer);
    }

public:
	AlgoCMD(const AlgoIdentifier &running, aulong signature)
        : algorithm(running.algorithm), implementation(running.implementation), versionHash(Hex(signature)), AbstractCommand("algo") { }
	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		// Specification mandates there should be 1 parameter of value "primary".
		// This is not really required (works perfectly with no params at all) but required for being future proof.
		using namespace rapidjson;
		std::string mode("primary");
		Value::ConstMemberIterator params = input.FindMember("params");
		if(params == input.MemberEnd() || params->value.IsNull()) { }
		else if(params->value.IsString()) { mode.assign(params->value.GetString(), params->value.GetStringLength()); }
		else {
			build.SetString("!!ERROR: \"parameters\" specified, but not a valid format.");
			return nullptr;
		}
		if(mode != "primary") {
			std::string msg("!!ERROR: \"parameters\" unrecognized value \"" + mode + "\"");
			build.SetString(msg.c_str(), rapidjson::SizeType(msg.length()), build.GetAllocator());
			return nullptr;
		}
		build.SetObject();
		if(algorithm.empty()) return nullptr; // the values are persistent as long as the parser is. Parsers go down after services have been stopped so,
		build.AddMember("algo", StringRef(algorithm.c_str()), build.GetAllocator());
        build.AddMember("impl", StringRef(implementation.c_str()), build.GetAllocator());
        build.AddMember("version", StringRef(versionHash.c_str()), build.GetAllocator());
		return nullptr;
	}
};


}
}
