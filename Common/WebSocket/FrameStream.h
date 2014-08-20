/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Network.h"
#include "ByteStream.h"

namespace ws {




//! Take frames of the WebSocket protocol and either mangle them (control frames) or keep building the message.
class FrameStream : private ByteStream {
public:
	FrameStream(Network::ConnectedSocketInterface &pipe) : ByteStream(pipe) { }
};

}
