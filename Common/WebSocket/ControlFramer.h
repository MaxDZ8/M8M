/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "Framer.h"

namespace ws {

/*! Takes care of managing the control frames. Code using this will never get a control frame to mangle.
What it does however is to signal connection close frames, with derived classes going to define the connection close management behaviour. */
class ControlFramer : public Framer {
public:
	ControlFramer(Network::ConnectedSocketInterface &pipe, bool requireMaskedPackets) : Framer(pipe, requireMaskedPackets) { }

protected:
	aubyte* FrameDataUpdated(asizei newData) {
		aubyte *ret = Framer::FrameDataUpdated(newData);
		if(!ret) return nullptr;
		const FrameType type = GetFrameType();
		if(!IsControlFrame(type)) return ret;
		asizei plLen = asizei(Framer::GetPayloadLen());
		const char *hex = "0123456789abcdef";
		switch(type) {
		case ft_close: {
			if(plLen != 2) ClosePacketReceived(cr_reserved_no_status); //!< \todo Not really correct. I should abort. Do I care?
			else {
				aushort reason;
				memcpy_s(&reason, sizeof(reason), ret, 2);
				ClosePacketReceived(reason);
			}
			break;
		}
		case ft_ping: SendPong(ret, asizei(GetPayloadLen())); break;
		case ft_pong: break; // nothing for that. Just ignore it.
		default: throw std::string("Received unrecognized control frame 0x") + hex[type & 0x0F];
		}
		return nullptr;
	}
};


}
