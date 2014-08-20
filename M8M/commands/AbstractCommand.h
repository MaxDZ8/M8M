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
	virtual PushInterface* Parse(Json::Value &reply, const Json::Value &input) = 0;

public:
	const std::string name;
	/*! This call gives a chance to each command to consume the input. Implementations must set the resulting reply only if they recognize their own properties.
	If an object does not modify the reply message nor produces a PushInterface object then the input message is ignored and no action is taken.
	Implementations can assume the passed string is empty on input.
	Commands which are guaranteed to never produce a PushInterface object have by convention "CMD" at the end of the name, implying they are "instant commands". */
	PushInterface* Mangle(std::string &result, const Json::Value &input) {
		#if defined(_DEBUG)
		std::string inputString(input.toStyledString());
		#endif
		if(input["command"].isString() == false ||  input["command"] != name) return nullptr;
		Json::Value reply;
		std::unique_ptr<PushInterface> ret;
		try {
			ret.reset(Parse(reply, input));
		} catch(std::string &what) {
			result = std::string("!!ERROR: ") + what + "!!";
			return nullptr;
		}
		// This is not really necessary as we respond to commands in order!
		//reply["command"] = name;
		if(reply.isNull() == false) result = reply.toStyledString();
		else result = "!!error!!";
		return ret.release();
	}
	virtual ~AbstractCommand() { }
};

}
