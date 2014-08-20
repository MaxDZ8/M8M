/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "ControlFramer.h"


namespace ws {

/*! Once the TCP-HTTP connection has been initialized and upgraded to WS, get the rid of the initializer object and build one of those.
It is a full, real websocket peer end, it is a stream of messages. */
class Connection : private ControlFramer {
public:
	const static asizei MAX_INBOUND_MESSAGE_SIZE;
	std::function<bool()> closeRequestCallback; //!< return true to have a EnqueueClose called automatically
	Connection(NetworkInterface::ConnectedSocketInterface &pipe, bool maskedPayloadRequired) : ControlFramer(pipe, maskedPayloadRequired) { }
	typedef std::vector<aubyte> Message;

	//! Returns true if at least a Message is now available to caller.
	bool Read(std::vector<Message> &data);

	bool NeedsToSend() const { return ControlFramer::NeedsToSend(); }
	void Send() { ControlFramer::Send(); }
	
	void EnqueueTextMessage(const char *msg, asizei len) { ControlFramer::EnqueueTextMessage(msg, len); }
	void EnqueueTextMessage(const std::string &msg) { EnqueueTextMessage(msg.c_str(), msg.length()); }

	bool Closed() const { return ControlFramer::Closed(); }

		/*
	void Close();
	*/

private:
	std::vector<aubyte> message;
	void CloseRequested(CloseReason reason, const aubyte *body, asizei byteCount) {
		bool close = true;
		if(closeRequestCallback) close = closeRequestCallback();
		if(close) {
			if(reason == cr_reserved_no_status) EnqueueClose(cr_done, nullptr, 0);
			else EnqueueClose(reason, body, byteCount);
		}
	}
};

}
