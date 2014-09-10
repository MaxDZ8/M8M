/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractCommand.h"
#include <memory>

namespace commands {

/*! Streaming commands are all those commands which will result in a streaming sequence of PUSH messages server->client.
They are specified by name just like usual "instant" commands but they are able to internally produce and pass out a PushInterface object
which will produce messages in the form of "command!" instead of "command?". */
	class AbstractStreamingCommand : public AbstractCommand {
public:
	AbstractStreamingCommand(const char *commandName) : AbstractCommand(commandName) { }

	PushInterface* Parse(rapidjson::Document &reply, const rapidjson::Value &input) {
		// A not very efficient way to produce streaming commands is to build their pusher anyway and then eventually drop it.
		// This isn't much of a problem in practice as 99% of the times a streaming command will have streaming on.
		std::unique_ptr<AbstractInternalPush> helper(NewPusher());
		helper->MangleMatched(reply, input);
		const rapidjson::Value::ConstMemberIterator push = input.FindMember("push");
		return push != input.MemberEnd() && push->value.IsBool() && push->value.GetBool()? helper.release() : nullptr;
	}
	
	asizei GetMaxPushing() const { return 1; }

protected:
	class AbstractInternalPush : public PushInterface {
	public:
		virtual ~AbstractInternalPush() { }

		/*! This function is used by the command on firstly generating the PUSH request. The signature has been already checked to match the command
		and is thus not conforming to PUSH syntax. */
		void MangleMatched(rapidjson::Document &out, const rapidjson::Value &input) {
			std::string error;
			SetState(input);
			RefreshAndReply(out, true);
		}

		bool Refresh(rapidjson::Document &out) {
			return RefreshAndReply(out, false);
		}

		/*! Given current state (what to monitor) tick internal logic to refresh your values. If those values changed, produce output. 
		Because a reply must always be given when input from user is received, you must consider this a change in itself and give output in that case.
		This is also called by the command generating this PUSH when replying to a request.
		To avoid expensive copies, build directly in the result and return false to discard that value. */
		virtual bool RefreshAndReply(rapidjson::Document &result, bool forcedOutput) = 0;
	
	protected:
		//! Parsing original command request, validation should happen here!
		virtual void SetState(const rapidjson::Value &object) = 0;
	};

	virtual AbstractInternalPush* NewPusher() = 0;
};

}
