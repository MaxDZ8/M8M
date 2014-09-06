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
	PushInterface* Parse(Json::Value &reply, const Json::Value &input) {
		const Json::Value &params(input["params"]);
		if(params.isObject() == false) throw std::exception("\"upgrade\", .parameters must be object.");
		const Json::Value &mode(params["mode"]);
		const Json::Value &list(params["list"]);
		if(mode.isString() == false) throw std::exception("\"upgrade\", parameters.mode must be string.");
		if(list.isArray() == false) throw std::exception("\"upgrade\", parameters.list must be array.");
		for(asizei loop = 0; loop < list.size(); loop++) {
			if(list[loop].isString() == false) throw std::exception("\"upgrade\", parameters.list contains non-string value.");
		}
		if(mode.asString() == "query") {
			reply = Json::Value(Json::objectValue);
			for(asizei loop = 0; loop < list.size(); loop++) {
				auto match = lister.find(list[loop].asString());
				reply[list[loop].asString()] = match != lister.cend();				
			}
		}
		else if(mode.asString() == "enable") {
			for(asizei init = 0; init < list.size(); init++) {
				auto match = lister.find(list[init].asString());
				if(match == lister.end()) break;
				if(match->second.disabled) {
					match->second.enable();
					match->second.disabled = false;
				}
			}
			reply = true;
		}
		else throw std::exception("\"upgrade\", parameters.mode unrecognized value.");
		return nullptr;
	}
};


}
