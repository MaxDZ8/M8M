/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractCommand.h"

namespace commands {


class VersionCMD : public AbstractCommand {
	PushInterface* Parse(Json::Value &reply, const Json::Value &input) {
		reply = "v1";
		return nullptr;
	}
public:
	VersionCMD() : AbstractCommand("version") { }
};


}
