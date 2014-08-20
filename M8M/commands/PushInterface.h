/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../../Common/AREN/ArenDataTypes.h"
#include <string>
#include <json/json.h>

namespace commands {

/*! Most commands just reply to a query.
Some others instead produce a stream of replies.
Those objects generate something implementing this as a result of their AbstractCommand::Mangle call.
The returned object is associated to the original source. Whatever that source produces additional data, the pending 'push' requests will have a chance at
intercepting the data. This gives them the chance to modify the internal state or request deletion.
If no request registered to the source consumes the data then it is forwarded to the set of AbstractCommand.
\note This isn't really a "Push request" but more like the structure holding the state which results in the various Push actions by server...
PushRequest if you want. */
class PushInterface {
public:
	virtual ~PushInterface() { }

	enum ReplyAction {
		ra_notMine, //!< either no push message or to be tested with other push requests, eventually going to AbstractCommand
		ra_delete,  //!< message was mine and instructed to get the rid of me
		ra_consumed, //!< mangled the message, not to be forwarded to AbstractCommand
		// ra_pass	// do not look for other push but go to AbstractCommand... which would mangle them anyway?

		ra_bad //!< it is mine but using bad syntax.
	};

	/*! This is called to react to network input. It is used by the manager to which PushInterface consumes this input.
	When an input command string does not match any registered command, the active PUSH requests are evaluated in sequence.
	Return ra_notMine if the given input does not belong to you. This will make the outer code keep trying other PUSH requests.
	If no PUSH consumes the message, then it is considered unmatched and an error is replied to client.
	Both ra_delete and ra_consumed signal ownership and stop the scanning process.
	Returning ra_bad produces similar behaviour as unmatched PUSH, except the message sent is the one produced, if any.

	Always produce a reply from this, even when the state has not changed. This keeps the protocol simple and allows push requests
	to be used as some sort of heartbeat shall the need arise. */
	virtual ReplyAction Mangle(std::string &out, const Json::Value &object) = 0;

	/*! This is called every time possible to check state against previous tick state.
	The only "input" here is passing time and what we want to know is a message to push to clint IF the contents changed.
	However, since Mangle immediately produces a reply implementations should not need to keep track of changes to input/settings but
	only those produced by ticking the logic inside here.
	If state didn't change, pass out an empty string. Since 0-length messages are not supported by protocol, it's considered nothing to do. */
	virtual void Refresh(std::string &out) = 0;
};


}
