/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "ControlFramer.h"


namespace ws {

/*! Once the TCP-HTTP connection has been initialized and upgraded to WS, get the rid of the initializer object and build one of those.
It is a full, real websocket peer end, it is a stream of messages. */
class Connection : public ControlFramer {
public:
	const static asizei GetMaxInboundMessageSize() { return 4 * 1024 * 1024; } // so I don't need a .cpp for static
	Connection(NetworkInterface::ConnectedSocketInterface &pipe, bool maskedPayloadRequired) : ControlFramer(pipe, maskedPayloadRequired) { }
	typedef std::vector<aubyte> Message;

	//! Returns true if at least a Message is now available to caller.
	bool Read(std::vector<Message> &data) {
		bool first = true;
		const asizei initially = data.size();
		while(true) {
			aubyte *raw = first? ControlFramer::Read() : Next();
			if(!raw) break;
			first = false;
			aulong count = GetPayloadLen();
			asizei used = message.size();
			if(count + used > GetMaxInboundMessageSize()) throw std::exception("Message is way too big!");
			message.resize(message.size() + asizei(count));
			memcpy_s(message.data() + used, message.size() - used, raw, asizei(count));
			if(IsFinalFrame()) data.push_back(std::move(message));
		}
		return data.size() != initially;
	}

private:
	std::vector<aubyte> message;
};

}
