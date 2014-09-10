/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../../Common/AREN/ArenDataTypes.h"
#include <string>
#include <rapidjson/document.h>

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

	/*! This is called every time possible to check state against previous tick state.
	The only "input" here is passing time and what we want to know is a message to push to clint IF the contents changed.
	\return false if nothing to send, otherwise true and a valid JSON value to be used as payload.
	\note For rapidjson, reply must be a Document (albeit it's a Value) so it can go along with its own allocator. */
	virtual bool Refresh(rapidjson::Document &out) = 0;
};


}
