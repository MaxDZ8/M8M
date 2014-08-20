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
	class AbstractStreamingCommand : public AbstractCommand{
public:
	AbstractStreamingCommand(const char *commandName) : AbstractCommand(commandName) { }

	PushInterface* Parse(Json::Value &reply, const Json::Value &input) {
		// A not very efficient way to produce streaming commands is to build their pusher anyway and then eventually drop it.
		// This isn't much of a problem in practice as 99% of the times a streaming command will have streaming on.
		std::unique_ptr<AbstractInternalPush> helper(NewPusher());
		PushInterface::ReplyAction result = helper->MangleMatched(reply, input, true);
		// ra_consumed or ra_delete with pusher already disabled.
		bool push = input["push"].isConvertibleTo(Json::booleanValue) && input["push"].asBool();
		return push? helper.release() : nullptr;
	}

protected:
	class AbstractInternalPush : public PushInterface {
	public:
		virtual ~AbstractInternalPush() { }

		//! This function is used by the command on firstly generating the PUSH request. The signature has been already checked to match the command
		//! and is thus not conforming to PUSH syntax. For this reason, a flag can assume it matched already.
		ReplyAction MangleMatched(Json::Value &out, const Json::Value &input, bool matched) {
			if(!matched) {
				const Json::Value &cmd(input["command"]);
				if(!cmd.isString()) { out = "!!ERROR: missing command string!!";    return ra_bad; }
				else if(!MyCommand(cmd.asString())) return ra_notMine;
			}
			std::string error;
			#if defined(_DEBUG)
			const std::string watcha(input.toStyledString());
			#endif
			ReplyAction ret = SetState(error, input);
			if(ret == ra_consumed) RefreshAndReply(out, true);
			else if(ret == ra_bad) out = std::string("!!ERROR: ") + error + "!!";
			return ret;
		}

		ReplyAction Mangle(std::string &out, const Json::Value &object) {
			Json::Value fill;
			ReplyAction result = MangleMatched(fill, object, false);
			out = std::move(fill.toStyledString());
			return result;
		}

		void Refresh(std::string &out) { 
			Json::Value object;
			if(RefreshAndReply(object, false)) {
				if(object["pushing"].isNull()) object["pushing"] = GetPushName();
				out = object.toStyledString();
			}
		}

		//! This helper struct will come handy for derived classes in setting their sub-fields without pissing themselves off too much.
		struct Changer {
			bool &different;
			Changer(bool &cumulate) : different(cumulate) { }

			template<typename ValueType>
			void Set(Json::Value &dst, const char *key, const ValueType &ref, const ValueType &src) {
				different |= src != ref;
				dst[key] = src;
			}
		};

		/*! Given current state (what to monitor) tick internal logic to refresh your values. If those values changed, produce output. 
		Because a reply must always be given when input from user is received, you must consider this a change in itself and give output in that case.
		This is also called by the command generating this PUSH when replying to a request.
		To avoid expensive copies, build directly in the result and return false to discard that value.
		\note This function should NEVER produce an error, validation should prevent that and the current protocol does not permit the client to understand
		if an error came from a pusher or a command-reply. It assumes it's from a command-reply, thereby implying queue state will be messed up big way
		if a pusher ever gives out an error! 
		\todo This is very brittle, the protocol shall be updated! (designed?) */
		virtual bool RefreshAndReply(Json::Value &result, bool forcedOutput) = 0;
	
	protected:
		//! Using a virtual function allows for sub-pushes to exist (for example one idependant push for each device... example: "deviceStats_0!"
		virtual bool MyCommand(const std::string &signature) const = 0;

		//! Parsing (a little bit redundant with MyCommand) and mostly state-setting. Validation should happen here!
		//! \note The error string is simplified there. It will be automatically put inside the error message syntax so just write a message.
		virtual ReplyAction SetState(std::string &error, const Json::Value &object) = 0;

		/*! Used when refreshing to send a specific push identifier (likely produced by RefreshAndReply) to populate the "command" parameter.
		If a command is issued by "commandName?" then the string to be included in pushes will have to start with "commandName!". Those are put both
		in the message ".pushing" subfield. This allows to discriminate between values streamed, messages to commands and streaming re-set. That is
		1) peer A requests command "cmd?" resulting in a stream.
		2) first message sent back by peer B is contains .command: "cmd?" as confirmation.
		3) following messages B->A with .pushing: "cmd!"
		4) for client to change properties of a push already active peer A sends .command: "cmd!<...>" */
		virtual std::string GetPushName() const = 0;
	};

	virtual AbstractInternalPush* NewPusher() = 0;
};

}
