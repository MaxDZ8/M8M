/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCommand.h"

namespace commands {
namespace admin {

struct RawConfig {
	/*! If config can be parsed good.IsObject() will return true and contain the parsed object.
	This only means it's synctactically correct. The values however might still be wrong so those errors are pooled
	in the valueErrors array.
	Otherwise, object cannot even be parsed. In that case there will be no good json value to inspect but there will be
	valid values in raw, errDesc, errOff, errCode. */
	rapidjson::Document good;
	std::vector<std::string> valueErrors;

	std::string raw, errDesc;
	asizei errOff;
	auint errCode;
};

class GetRawConfigCMD : public AbstractCommand {
public:
	const RawConfig &track; //!< I could really just copy it there but rapidjson::Value has move semantics and I don't want to mess up outer state nor copy
	GetRawConfigCMD(const RawConfig &loaded) : track(loaded), AbstractCommand("getRawConfig") { }

	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		using namespace rapidjson;
		build.SetObject();
		if(track.good.IsObject()) {
			// copy anyway, a bit unfortunate
			build.AddMember("configuration", Value(track.good, build.GetAllocator()), build.GetAllocator());
			if(track.valueErrors.size()) {
				Value errors(kArrayType);
				errors.Reserve(rapidjson::SizeType(track.valueErrors.size()), build.GetAllocator());
				for(asizei loop = 0; loop < track.valueErrors.size(); loop++) {
					Value add(track.valueErrors[loop].c_str(), build.GetAllocator());
					errors.PushBack(add, build.GetAllocator());
				}
				build.AddMember("errors", errors, build.GetAllocator());
			}
		}
		else {
			build.AddMember("raw", Value(track.raw.c_str(), build.GetAllocator()), build.GetAllocator());
			build.AddMember("errorDesc", Value(track.errDesc.c_str(), build.GetAllocator()), build.GetAllocator());
			build.AddMember("errorOffset", track.errOff, build.GetAllocator());
			build.AddMember("errorCode", Value(track.errCode), build.GetAllocator());
		}
		return nullptr;
	}
};


}
}
