/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractCommand.h"

namespace commands {


/*! Basically glue to parse out json so real pusher manager does not need to mess up with parsing. */
class UnsubscribeCMD : public AbstractCommand {
public:
	class PusherOwnerInterface {
	public:
		virtual ~PusherOwnerInterface() { }
		//! Remove a pusher from the list of active pushers and destroy all resources before returning.
		//! Messages already queued can stay. If stream is empty, destroy everything from the specified command.
		//! Command guaranteed to be non-null. Destroying something non-existing is silently NOP.
		virtual void Unsubscribe(const std::string &command, const std::string &stream) = 0;
	};
	PusherOwnerInterface &owner;
	UnsubscribeCMD(PusherOwnerInterface &manager) : owner(manager), AbstractCommand("unsubscribe") { }

protected:
	PushInterface* Parse(Json::Value &reply, const Json::Value &input) {
		const Json::Value &params(input["params"]);
		if(params.isObject() == false) throw std::exception("\"unsubscribe\", .parameters must be object.");
		if(params["originator"].isString() == false) throw std::exception("\"unsubscribe\", .parameters.originator missing or not a string.");
		if(params["stream"].isNull()) owner.Unsubscribe(params["originator"].asString(), std::string(""));
		else if(params["stream"].isConvertibleTo(Json::stringValue) == false) throw std::exception("\"unsubscribe\", .parameters.stream must be convertible to a string if specified.");
		else owner.Unsubscribe(params["originator"].asString(), params["stream"].asString());
		reply = true;
		return nullptr;
	}
};


}
