/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCommand.h"

namespace commands {
namespace admin {

class ConfigFileCMD : public AbstractCommand {
public:
	struct ConfigInfoProviderInterface {
		virtual std::wstring Filename() const = 0;
		virtual bool Explicit() const = 0;
		virtual bool Redirected() const = 0;
		virtual bool Valid() const = 0;
		virtual ~ConfigInfoProviderInterface() { }
	};
	ConfigFileCMD(ConfigInfoProviderInterface &src) : config(src), AbstractCommand("configFile") { }

	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		// Specification mandates there should be 1 parameter of value "primary".
		// This is not really required (works perfectly with no params at all) but required for being future proof.
		using namespace rapidjson;
		build.SetObject();
		const std::wstring name(config.Filename());
		GenericStringStream< UTF16<> > source(name.c_str());
		GenericStringBuffer<UTF8<> > target;
		//bool hasError = false; <-- would have failed decoding previously?
		while (source.Peek() != '\0') {
			if (!Transcoder< UTF16<>, UTF8<> >::Transcode(source, target)) {
				//hasError = true;
				break;
			}
		}
		build.AddMember("filename", Value(target.GetString(), rapidjson::SizeType(target.GetSize()), build.GetAllocator()), build.GetAllocator());
		build.AddMember("explicit", Value(config.Explicit()), build.GetAllocator());
		build.AddMember("redirected", config.Redirected(), build.GetAllocator());
		build.AddMember("valid", config.Valid(), build.GetAllocator());
		return nullptr;
	}

private:
	ConfigInfoProviderInterface &config;
};


}
}
