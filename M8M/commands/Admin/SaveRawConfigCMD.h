/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCommand.h"
#include "../../../Common/AREN/SharedUtils/dirControl.h"
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <fstream>

namespace commands {
namespace admin {

class SaveRawConfigCMD : public AbstractCommand {
public:
	const std::wstring confDir;
	const std::wstring filename;
	SaveRawConfigCMD(const wchar_t *cfgDir, const wchar_t *currentFile) : confDir(cfgDir), filename(currentFile), AbstractCommand("saveRawConfig") { }

	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		using namespace rapidjson;
		Value::ConstMemberIterator params = input.FindMember("params");
		if(params == input.MemberEnd() || params->value.IsObject() == false) throw std::exception("Missing .params object.");
		sharedUtils::system::AutoGoDir<true> cfgDir(confDir.c_str(), true);
		std::wstring target;
		Value::ConstMemberIterator dst = params->value.FindMember("destination");
		if(dst == input.MemberEnd() || dst->value.IsString() == false) target = filename;
		else {
			const std::string utfByte(dst->value.GetString(), dst->value.GetStringLength());
			StringStream source(utfByte.c_str());
			GenericStringBuffer<UTF16<> > wide;
			bool hasError = false;
			while (source.Peek() != '\0') {
				if (!Transcoder< UTF8<>, UTF16<> >::Transcode(source, wide)) {
					hasError = true;
					break;
				}
			}
			if (hasError) throw "Invalid UTF8 sequence detected in JSON input."; // is this possible?
			std::wstring buff(wide.GetString(), wide.GetSize());
			target = std::move(buff);
		}
		Value::ConstMemberIterator cfg = params->value.FindMember("configuration");
		if(cfg == input.MemberEnd() || cfg->value.IsObject() == false) {
			build.SetBool(false);
			return nullptr;
		}
		StringBuffer buff;
		PrettyWriter<StringBuffer> writer(buff, nullptr);
		cfg->value.Accept(writer);

		std::ofstream out(target);
		const asizei buffsize = buff.GetSize();
		const asizei len = strlen(buff.GetString());
		out.write(buff.GetString(), buff.GetSize());

		build.SetBool(true);
		return nullptr;
	}
};


}
}
