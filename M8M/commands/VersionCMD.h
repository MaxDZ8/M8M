/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractCommand.h"

namespace commands {


class VersionCMD : public AbstractCommand {
	PushInterface* Parse(rapidjson::Document &reply, const rapidjson::Value &input) {
		reply.SetString("v1.1");
		return nullptr;
	}
public:
	VersionCMD() : AbstractCommand("version") { }
};


}
