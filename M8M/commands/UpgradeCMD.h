/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractCommand.h"
#include "ExtensionState.h"
#include <map>

namespace commands {


class UpgradeCMD : public AbstractCommand {
public:
	std::map<std::string, ExtensionState> &lister;
	UpgradeCMD(std::map<std::string, ExtensionState> &read) : lister(read), AbstractCommand("upgrade") { }

protected:
	PushInterface* Parse(rapidjson::Document &reply, const rapidjson::Value &input) {
		using namespace rapidjson;
		Value::ConstMemberIterator &params(input.FindMember("params"));
		if(params == input.MemberEnd() || params->value.IsObject() == false) throw std::exception("\"upgrade\", .parameters must be object.");
		Value::ConstMemberIterator &mode(params->value.FindMember("mode"));
		Value::ConstMemberIterator &list(params->value.FindMember("list"));
		if(mode == params->value.MemberEnd() || mode->value.IsString() == false) throw std::exception("\"upgrade\", parameters.mode must be string.");
		if(list == params->value.MemberEnd() || list->value.IsArray() == false) throw std::exception("\"upgrade\", parameters.list must be array.");
		for(SizeType loop = 0; loop < list->value.Size(); loop++) {
			if(list->value[loop].IsString() == false) throw std::exception("\"upgrade\", parameters.list contains non-string value.");
		}
		const std::string test(std::string(mode->value.GetString(), mode->value.GetStringLength()));
		if(test == "query") {
			reply.SetObject();
			for(SizeType loop = 0; loop < list->value.Size(); loop++) {
				const std::string str(list->value[loop].GetString(), list->value[loop].GetStringLength());
				auto match = lister.find(str);
				reply.AddMember(StringRef(list->value[loop].GetString()), match != lister.cend(), reply.GetAllocator());
			}
		}
		else if(test == "enable") {
			for(SizeType init = 0; init < list->value.Size(); init++) {
				auto match = lister.find(std::string(list->value[init].GetString(), list->value[init].GetStringLength()));
				if(match == lister.end()) break;
				if(match->second.disabled) {
					match->second.enable();
					match->second.disabled = false;
				}
			}
			reply.SetBool(true);
		}
		else throw std::exception("\"upgrade\", parameters.mode unrecognized value.");
		return nullptr;
	}
};


}
