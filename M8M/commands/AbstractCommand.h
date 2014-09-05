/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../../Common/AREN/ArenDataTypes.h"
#include <json/json.h>
#include <string>
#include "PushInterface.h"

namespace commands {

/*! This interface is used by web interfaces (in the sense of web server) to mangle commands from the web socket and produce a result.
For the time being, a result is always produced syncronously. The server looks its registered commands iteratively until one with a matching name is found.
Then, it issues a Mangle(), which results in a string being a JSON'd object to reply. */
class AbstractCommand {
protected:
	AbstractCommand(const char *string) : name(string) { }

	//! If command does not support pushing, just return nullptr. It's perfectly valid.
	virtual PushInterface* Parse(Json::Value &reply, const Json::Value &input) = 0;

public:
	const std::string name;
	/*! This call gives a chance to each command to consume the input.
	The protocol mandates a reply to each message and optionally a push stream.
	Implementation must return true if they recognize the command as theirs.
	At this point, the protocol mandates to return a non-empty string (otherwise, it's considered a generic error).
	When non-empty string is returned, string is sent as is.
	Implementations must set the resulting reply only if they recognize their own properties.
	Implementations are suggested to produce a push streamer only if after result has been set. Push streamers with no reply produced
	are dropped anyway. */
	bool Mangle(std::string &result, std::unique_ptr<PushInterface> &streamer, const Json::Value &input) {
		#if defined(_DEBUG)
		std::string inputString(input.toStyledString());
		#endif
		if(input["command"].isString() == false ||  input["command"] != name) return false;
		Json::Value reply;
		std::unique_ptr<PushInterface> ret;
		try {
			ret.reset(Parse(reply, input));
		} catch(std::string &what) {
			result = std::string("!!ERROR: ") + what + "!!";
			return true;
		}
		if(input["push"].isConvertibleTo(Json::booleanValue) && input["push"].asBool()) {
			if(ret.get() == nullptr) {
				result = std::string("!!ERROR: command ") + name + " does not support pushing!!";
				return true;
			}
			streamer = std::move(ret);
		}
		result = reply.toStyledString();
		return true;
	}
	virtual ~AbstractCommand() { }

	/*! This is really used to trigger different mangling in the command manager.
	- If a command does not support pushes, then this values is 0;
	- If it supports at most 1 push, then the manager does not need to generate/send push identifiers with push messages.
	In all cases, when we have GetMaxPushing() pushes already active we must destroy one before generating a new one. */
	virtual asizei GetMaxPushing() const { return 0; }
};

}
