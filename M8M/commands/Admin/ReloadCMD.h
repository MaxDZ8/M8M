/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCommand.h"
#include "../../../Common/AREN/SharedUtils/dirControl.h"
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <functional>

namespace commands {
namespace admin {

class ReloadCMD : public AbstractCommand {
public:
	std::function<bool()> reloadRequested;
	ReloadCMD(std::function<bool()> callback) : reloadRequested(callback), AbstractCommand("reload") { }

	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		using namespace rapidjson;
		build.SetBool(reloadRequested());
		return nullptr;
	}
};


}
}
