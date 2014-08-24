/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Network.h"
#include <string>

namespace ws {

//! Take octects out of a connection and build a frame.
//! Also help outer code in mangling the frame itself and its payload but do not take initiative by yourself.
class Framer {
public:
	const static asizei MAX_INBOUND_FRAME_SIZE;
	const static asizei FRAME_REALLOCATION_INCREMENT;
	const bool server;
	Framer(Network::ConnectedSocketInterface &pipe, bool requireMaskedPackets)
		: socket(pipe), usedInbound(0), plLen(0), head(ft_tbd), firstFrame(true), server(requireMaskedPackets), nextPongSlot(0), sentOut(0) { }

	/*! Read octects from the socket. If a whole frame can be assembled, then returns non-null pointer to the payload.
	To know how much data is there, call GetPayloadLen. To know the frame type, call GetFrameType. */
	aubyte* Read();

	/*! Call this function to mark the current frame as processed so this can get the rid of it.
	If Read returned a valid pointer, then you must call this before another Read can be issued or state will be undefined.
	Because of how TCP works, two small frames might come one after another. In this case, this function will return data just as Read.
	From the point of view of the external code, the only difference is that it does not access the network. */
	aubyte* Next();

	// In general, the functions below are valid only if you got a pointer out of either Read() or Next().

	//! Returns 0 if there's no frame being received or we haven't received enough bytes.
	//! It is also possible for a frame to have 0 bytes of payload.
	aulong GetPayloadLen() const { return plLen; }

	//! If false is returned, then the payload is the last part of a message.
	bool IsFinalFrame() const;

	enum FrameType {
		ft_tbd, //!< bytes not yet mangled

		// continuation: not used
		ft_user_text,
		ft_user_binary,
		// 3,4,5,6,7 not used
		ft_close,
		ft_ping,
		ft_pong
		// bcdef not used
	};
	FrameType GetFrameType() const { return head; }

	static bool IsControlFrame(FrameType t) { return t >= ft_close; }


	enum CloseReason {
		cr_done = 1000,
		cr_away,
		cr_protoError,
		cr_badDataType,
		// cr_reserved_for_future_use_TBD = 1004,
		cr_reserved_no_status = 1005,
		cr_reserved_abnormal_termination_FORBIDDEN = 1006,
		cr_illFormedData = 1007,
		cr_badPolicy,
		cr_messageTooBig,
		cr_client_missingServerExtensions,
		cr_server_internalError
		// cr_reserved_TLSFAIL
	};

	void Send();

	//! The application here on this peer has requested to shut down.
	//! To do this, we must wait until the client replies with the shutdown packet.
	void EnqueueClose(CloseReason r);
	void EnqueueTextMessage(const char *msg, asizei len);
	void EnqueueTextMessage(const std::string &str) { EnqueueTextMessage(str.c_str(), str.length()); }

	bool NeedsToSend() const;

	enum WebSocketStatus {
		wss_operational,
		wss_waitingCloseReply,
		wss_sendingCloseConfirm,
		wss_closed
	};
	WebSocketStatus GetStatus() const;

protected:
	//! Mangle the data already in the frame. Called to produce the return value of both both Read() and Next().
	virtual aubyte* FrameDataUpdated(asizei newData);
	void SendPong(const aubyte *payload, asizei byteCount);

	/*! Derived classes call this when they receive a close packet from the peer.
	We might... or might not have to reply the reason depending on this being a close confirm or not.
	Since I just have to reply with those bytes it sent me, I don't care about converting them.
	\note Just pass the data here as received, with no ntohs or stuff */
	void ClosePacketReceived(aushort reason);

private:
	Network::ConnectedSocketInterface &socket;
	std::vector<aubyte> inbound; //!< notice length here is really the amount of allocated data (because I have to alloc it in advance to read it)
	asizei usedInbound;
	aulong plLen; //!< Frame length after extraction in bytes.
	bool firstFrame; //!< Set when receiving the first frame, all subsequent frames must have opcode = 0
	FrameType head; //!< type extracted from the first frame.

	std::vector<aubyte> outbound;
	asizei sentOut;

	static FrameType MakeFT(aubyte opcode);
	asizei HeaderByteCount() const;

	//! I try to reply only to last pong but if I'm already sending a pong, better finish it first!
	struct ControlFrame {
		std::vector<aubyte> payload;
		asizei sent;
		ControlFrame() : sent(0) { }
	} pong[2];
	asizei nextPongSlot; //!< When requested to send a new pong, put the data here. You can use the same pong slot however if no data has been sent yet.

	struct CloseFrame : ControlFrame {
		bool waitForReply, replyReceived;
		CloseFrame() : waitForReply(false), replyReceived(false) { }
	} closeFrame;
};

}
