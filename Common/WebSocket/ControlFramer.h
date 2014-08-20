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
	aubyte* FrameDataUpdated(asizei newData);
	virtual void CloseRequested(CloseReason reason, const aubyte *body, asizei byteCount) = 0;
};


}
