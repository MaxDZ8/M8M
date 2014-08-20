/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "ControlFramer.h"

namespace ws {


aubyte* ControlFramer::FrameDataUpdated(asizei newData) {
	aubyte *ret = Framer::FrameDataUpdated(newData);
	if(!ret) return nullptr;
	const FrameType type = GetFrameType();
	if(!IsControlFrame(type)) return ret;
	asizei plLen = asizei(Framer::GetPayloadLen());
	const char *hex = "0123456789abcdef";
	switch(type) {
	case ft_close: {
		if(plLen && plLen != 2) CloseRequested(cr_reserved_no_status, nullptr, 0); //!< \todo Not really correct. I should abort. Do I care?
		aushort reason = ntohs(*reinterpret_cast<aushort*>(ret));
		CloseRequested(static_cast<CloseReason>(reason), ret, plLen);
		break;
	}
	case ft_ping: SendPong(ret, asizei(GetPayloadLen())); break;
	case ft_pong: break; // nothing for that. Just ignore it.
	default: throw std::string("Received unrecognized control frame 0x") + hex[type & 0x0F];
	}
	return nullptr;
}


}
