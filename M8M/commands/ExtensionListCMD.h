/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractCommand.h"
#include "ExtensionState.h"
#include <map>

namespace commands {


class ExtensionListCMD : public AbstractCommand {
public:
	const std::map<std::string, ExtensionState> &lister;
	ExtensionListCMD(const std::map<std::string, ExtensionState> &read) : lister(read), AbstractCommand("extensionList") { }

protected:
	PushInterface* Parse(Json::Value &reply, const Json::Value &input) {
		reply = Json::Value(Json::arrayValue);
		auto ext = lister.cbegin();
		while(ext != lister.cend()) {
			if(ext->second.desc.length()) reply[reply.size()] = ext->second.desc;
			++ext;
		}
		return nullptr;
	}
};


}
