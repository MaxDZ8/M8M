/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "Framer.h"

namespace ws {


const asizei Framer::MAX_INBOUND_FRAME_SIZE = 1024 * 1024 * 4;
const asizei Framer::FRAME_REALLOCATION_INCREMENT = 1024;

aubyte* Framer::Read() {
	if(usedInbound >= inbound.size()) {
		if(inbound.size() >= MAX_INBOUND_FRAME_SIZE) throw std::string("WebSocket frame too big (max size is ") + std::to_string(MAX_INBOUND_FRAME_SIZE) + ')';
		inbound.resize(inbound.size() + FRAME_REALLOCATION_INCREMENT);
	}
	asizei got = socket.Receive(inbound.data() + usedInbound, inbound.size() - usedInbound);
	return FrameDataUpdated(got);
}


aubyte* Framer::Next() {
	const aulong frameSize = plLen + HeaderByteCount();
	asizei remaining = asizei(usedInbound) - asizei(frameSize);
	if(IsFinalFrame()) {
		firstFrame = true;
		head = ft_tbd;
	}
	else firstFrame = false;
	for(asizei cp = 0; cp < asizei(remaining); cp++) inbound[cp] = inbound[cp + asizei(frameSize)];
	plLen = usedInbound = 0;
	return FrameDataUpdated(remaining);
}


bool Framer::IsFinalFrame() const {
	return usedInbound? (inbound[0] & 0x80) != 0 : false;
}


void Framer::Send() {
	asizei ponging = nextPongSlot + 1;
	ponging %= 2;
	ControlFrame &maybe(pong[ponging]);
	if(maybe.payload.size()) {
		maybe.sent += socket.Send(maybe.payload.data() + maybe.sent, maybe.payload.size() - maybe.sent);
		if(maybe.sent == maybe.payload.size()) {
			maybe.sent = 0;
			maybe.payload.clear();
			ponging++;
		}
	}
	else if(closeFrame.payload.size()) {
		if(closeFrame.sent < closeFrame.payload.size()) {
			closeFrame.sent += socket.Send(closeFrame.payload.data() + closeFrame.sent, closeFrame.payload.size() - closeFrame.sent);
		}
		// otherwise, we are already closed and goodbye
	}
	else if(outbound.size()) {
		bool closeEnqueued = closeFrame.payload.size() != 0;
		sentOut += socket.Send(outbound.data() + sentOut, outbound.size() - sentOut);
		if(sentOut == outbound.size()) {
			outbound.clear();
			sentOut = 0;
		}
	}
}


void Framer::EnqueueClose(CloseReason r) {
	if(closeFrame.sent) return; // Only the first one means something if we started already.
	aushort reason = static_cast<aushort>(r);
	reason = htons(reason);
	closeFrame.payload.resize(sizeof(reason) + 2);
	const aubyte head = 0x88;
	const aubyte pllen = 0x02;
	memcpy_s(closeFrame.payload.data() + 0, closeFrame.payload.size() - 0, &head, sizeof(head));
	memcpy_s(closeFrame.payload.data() + 1, closeFrame.payload.size() - 1, &pllen, sizeof(pllen));
	memcpy_s(closeFrame.payload.data() + 2, closeFrame.payload.size() - 2, &reason, sizeof(reason));
	closeFrame.waitForReply = true;
}


void Framer::EnqueueTextMessage(const char *msg, asizei len) {
	if(closeFrame.payload.size()) {
		// It comes handy to just let the outer code think we're doing all our work here... While in fact we don't and
		// just shut down. In theory the outer code should not send us stuff anymore but being NOP is just more convenient.
		return;
	}
	// Due to the way TCP works, enqueuing is dead cheap. We just keep concatenating bytes.
	// Note due to this path being assumed trustworthy it can grow to massive sizes if connection goes belly up.
	if(len == 0) throw std::exception("Zero-sized messages are not supported!"); // in case you haven't got that.
	// There's no such thing as framing on the send-side. Framing happens by connection intermediaries.
	// In the future, I think I might frame on say 4MiB, but for the time being, I just send everything as is!
	aubyte header[1 + 1 + 8 + 4]; // max size
	asizei hbytes = 2;
	header[0] = 0x81;
	if(!server) header[1] = 0x80;
	else header[1] = 0;
	if(len <= 125) header[1] |= aubyte(len);
	else if(len < 64 * 1024) {
		header[1] |= 126;
		aushort extra = htons(aushort(len));
		memcpy_s(header + 2, sizeof(header) - 2, &extra, sizeof(extra));
		hbytes += 2;
	}
	else {
		header[1] |= 127;
		aulong extra = htonll(aulong(len));
		memcpy_s(header + 2, sizeof(header) - 2, &extra, sizeof(extra));
		hbytes += 8;
	}
	if(!server) {
		throw std::exception("TODO: get 4 random bytes as mask key from some high-entropy, possibly hardware source!");
	}
	const asizei initial = outbound.size();
	outbound.resize(initial + len + hbytes);
	memcpy_s(outbound.data() + initial, outbound.size() - initial, header, hbytes);
	memcpy_s(outbound.data() + initial + hbytes, outbound.size() - initial - hbytes, msg, len);
	if(!server) {
		throw std::exception("TODO: mask the octects being sent! (or just copy them by masking)");
	}
}


bool Framer::NeedsToSend() const {
	asizei ponging = nextPongSlot + 1;
	ponging %= 2;
	if(closeFrame.payload.size() && closeFrame.sent == closeFrame.payload.size()) return false;
	bool got = closeFrame.payload.size() != 0 && closeFrame.sent < closeFrame.payload.size(); // this is kept to mark close state
	got |= pong[ponging].payload.size() != 0;
	got |= outbound.size() != 0;
	return got;
}


Framer::WebSocketStatus Framer::GetStatus() const {
	if(closeFrame.payload.size() == 0) return wss_operational;
	if(closeFrame.waitForReply) {
		return closeFrame.replyReceived? wss_closed : wss_waitingCloseReply;
	}
	return closeFrame.sent == closeFrame.payload.size()? wss_closed : wss_sendingCloseConfirm;
}


aubyte* Framer::FrameDataUpdated(asizei got) {
	if(got == 0) return nullptr;

	const asizei valid = usedInbound + got;
	if(valid && !usedInbound) {
		// check for extension bits
		if(inbound[0] & 0x70) throw std::exception("Extension bits set. Invalid packet."); //!< \todo too brittle! Throw a catch-able exception so client can be disconnected instead of crunching the server. Must fail the connection.
		
		const aubyte opcode = inbound[0] & 0x0F;
		if(firstFrame) head = MakeFT(opcode);
		else {
			/*! \todo Not true. A control opcode can be at any point in the frame stream, I should really check for that. I should really allow it to concatenate but this requires some additional logic.
			Better to think at it another while. Considering the control frames are very different in nature, perhaps another buffer should do... */
			if(opcode) throw std::exception("Only the first frame of a message can have an opcode."); //!< \todo too brittle! Throw a catch-able exception so client can be disconnected instead of crunching the server
		}

	}
	if(valid >= 2 && usedInbound < 2) {
		bool MASK = (inbound[1] & 0x80) != 0;
		if(!MASK && server) throw std::exception("Frame from client is unmasked. Not allowed."); //!< \todo This should really throw an easy-to intercept exception so I can catch it!
	}
	if(plLen == 0) {
		if(valid >= 2 && usedInbound < 2) {
			aulong len = inbound[1] & 0x7F;
			if(len <= 125) plLen = len;
			else {
				asizei extra = len == 126? 2 : 8;
				if(valid >= 2 + extra) {
					if(extra == 2) {
						aushort network;
						memcpy_s(&network, sizeof(network), inbound.data() + 2, 2);
						plLen = ntohs(network);
					}
					else {
						aulong network;
						memcpy_s(&network, sizeof(network), inbound.data() + 2, 8);
						plLen = ntohll(network);
					}
				}
			}
		}
	}
	usedInbound += got;
	const asizei dataOff = HeaderByteCount();
	if(usedInbound < plLen + dataOff) return nullptr;

	// Unmask the frame data. Note at this point presence of those bits have already been checked.
	if(server && GetFrameType() != ft_ping) { // It seems like the "application data" must be the same for pongs, not unkeyed...
		// The mask is a multi-byte sequence, not an integer so it does not need to be swapped, unless in the future I want to optimize this.
		aubyte mask[4];
		memcpy_s(mask, sizeof(mask), inbound.data() + dataOff - 4, 4);
		for(asizei loop = 0; loop < plLen; loop++) inbound[loop + dataOff] ^= mask[loop % 4];
	}
	return inbound.data() + HeaderByteCount();
}


void Framer::SendPong(const aubyte *payload, asizei byteCount) {
	/* I only reply to the most recent ping (which I assume the peer will discriminate using payload).
	So previous pongs get trashed, unless they're being sent. NextPongSlot is never updated by me, it is mangled by Send procedure. */
	ControlFrame &store(pong[nextPongSlot]);
	store.payload.resize(byteCount);
	memcpy_s(store.payload.data(), byteCount, payload, byteCount);
	store.sent = 0;
}


void Framer::ClosePacketReceived(aushort reason) {
	if(closeFrame.waitForReply) closeFrame.replyReceived = true;
	else if(closeFrame.payload.size() == 0) { // then I have to reply...
		closeFrame.payload.resize(sizeof(reason));
		memcpy_s(closeFrame.payload.data(), closeFrame.payload.size(), &reason, sizeof(reason));
		closeFrame.waitForReply = false; // default anyway
	} else {
		// ? I just ignore it
	}
}


Framer::FrameType Framer::MakeFT(aubyte opcode) {
	const char *hex = "0123456789abcdef";
	switch(opcode) {
	case 0x1: return ft_user_text;
	case 0x2: return ft_user_binary;
	// case 0x3:
	// case 0x4:
	// case 0x5:
	// case 0x6:
	// case 0x7: throw std::string("Unrecognized non-control opcode 0x") + hex[opcode];
	case 0x8: return ft_close;
	case 0x9: return ft_ping;
	case 0xA: return ft_pong;
	// case 0xB:
	// case 0xC:
	// case 0xD:
	// case 0xE:
	// case 0xF: throw std::string("Unrecognized control opcode 0x") + hex[opcode];
	}
	std::string error(opcode < 0x08? "Unrecognized non-control opcode 0x" : "Unrecognized control opcode 0x");
	throw error + hex[opcode];
}


asizei Framer::HeaderByteCount() const {
	asizei sure = 1 + 1; // bits,opcode,   MASK,payload
	if(server) sure += 4; // masking bits
	if(plLen <= 125) return sure;
	return sure + (plLen == 126? 2 : 8);
}

}
