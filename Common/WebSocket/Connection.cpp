/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "Connection.h"

namespace ws {


const asizei Connection::MAX_INBOUND_MESSAGE_SIZE = MAX_INBOUND_FRAME_SIZE * 16;


bool Connection::Read(std::vector<Message> &data) {
	bool first = true;
	const asizei initially = data.size();
	while(true) {
		aubyte *raw = first? ControlFramer::Read() : Next();
		if(!raw) break;
		first = false;
		aulong count = GetPayloadLen();
		asizei used = message.size();
		if(count + used > MAX_INBOUND_MESSAGE_SIZE) throw std::exception("Message is way too big!");
		message.resize(message.size() + asizei(count));
		memcpy_s(message.data() + used, message.size() - used, raw, asizei(count));
		if(IsFinalFrame()) data.push_back(std::move(message));
	}
	return data.size() != initially;
}


	/*
bool Connection::NeedsToSend() const {
	return false;
}


void Connection::Send() {
	return;
}


void Connection::Close() {
	return;
}

*/

}
