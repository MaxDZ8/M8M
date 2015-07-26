/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Network.h"
#include <string>
#include <algorithm>
#include "../hashing.h"
#include <sstream>

namespace ws {

/*! This class is a bit silly. What it does is to estabilish WebSocket protocol over a TCP connection. It takes care of the opening handshake.
It does NOT take care of the closing handshake as this is a higher-level operation inside the WebSocket protocol which needs framing.
Usage: the outer code has a port in "listen" state. When a connect is detected, an HandShaker object on the new client "accepte'd" port
gets created.
\note Ownership of the connection port is never in this object. Something else creates it, something else will destroy it.
This also allows to avoid dependancies on the NetworkInterface class. The outer code needs the port anyway to sleep on it.

After handshake completed, you can just dispose of this object. The obtained pipe will now be a real websocket.
Therefore, we don't have an object implementing WebSockets for real but I honestly like the idea. Implementing part of HTTP is no joke either. */
class HandShaker {
public:
	static const asizei maxHeaderBytes;
	const std::string protocol, resource;
	HandShaker(Network::ConnectedSocketInterface &pipe, const std::string &protocolString, const std::string &uri)
		: stream(pipe), used(0), protocol(protocolString), resource(uri), sent(0) { }

	/*! Call this if the port has been signaled to have bytes to be read.
	What it does: collect bytes till obtaining a proper HTTP header requesting switch to WebSockets and process it.
	For security reasons there's a limit on how big the header can be.
	\note Because of how the handshake works, a compliant client will never send exceeding bytes. Non-compliant clients sending more are not a concern and those bytes will be lost.
	\sa maxHeaderBytes
	\todo Better name. MangleReceivedData? ReadNMangle? Input()?*/
	void Receive();

	//! This returns true after Receive has mangled the header --> it returned true.
	bool NeedsToSend() const { return response.length() > 0 && sent < response.length(); }

	//! Call this only if NeedsToSend returned true.
	void Send() { sent += stream.Send(response.data() + sent, response.length() - sent); }

	//! \returns true if handshake completed and this object is no more necessary. Socket is now WebSocket protocol. Same value returned by previous Read() call.
	bool Upgraded() { return response.length() > 0 && sent == response.length(); }

private:
	static const asizei headerIncrementBytes;
	static const char CR, LF;
	static bool LWS(char c) { return c == ' ' || c == '\t'; }
	static bool DIGIT(char c) { return c >= '0' && c <= '9'; }
	static std::string GetHeaderValue(const std::vector<std::string> &lines, const char *name);
	static std::vector<std::string> Split(const std::string &list, const char separator);
	Network::ConnectedSocketInterface &stream;
	/*! If you read the specifications, those should really be octects (as the characters are sometimes to be mangled case-insensitively, sometimes not) but that's just easier.
	Note the vector contains the amount of /allocated/ bytes, not /valid/, comes handy to reallocate even though it's a bit ugly and against the idea of vector. */
	std::vector<char> header;
	asizei used;
	std::string response; //!< Populated by Receive() as soon as
	std::string key;
	asizei sent;
};

}
