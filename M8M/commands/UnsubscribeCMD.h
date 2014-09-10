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
	PushInterface* Parse(rapidjson::Document &reply, const rapidjson::Value &input) {
		using namespace rapidjson;
		Value::ConstMemberIterator &params(input.FindMember("params"));
		if(params == input.MemberEnd() || params->value.IsObject() == false) throw std::exception("\"unsubscribe\", .parameters must be object.");
		Value::ConstMemberIterator &ori(params->value.FindMember("originator"));
		Value::ConstMemberIterator &stream(params->value.FindMember("stream"));
		if(ori == params->value.MemberEnd() || ori->value.IsString() == false) throw std::exception("\"unsubscribe\", .parameters.originator missing or not a string.");
		const std::string cmd(ori->value.GetString(), ori->value.GetStringLength());
		if(stream == params->value.MemberEnd()) owner.Unsubscribe(cmd, std::string());
		else if(stream->value.IsString() == false) throw std::exception("\"unsubscribe\", .parameters.stream must be convertible to a string if specified.");
		else owner.Unsubscribe(cmd, std::string(stream->value.GetString(), stream->value.GetStringLength()));
		reply.SetBool(true);
		return nullptr;
	}
};


}
