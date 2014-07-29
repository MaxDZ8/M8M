/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
/* I usually put static variables in a cpp file nearby the header using it.
So, if class Something needs a static Something::blah, and it's in the Static.h
file, I would put blah in Static.cpp.
However, this does not scale much and I think it makes sense to pool all those
special variables in a single place so they can be monitored more easily. */
#include "Network.h"


std::unique_ptr< std::map<int, SockErr> > WindowsNetwork::errMap;
size_t NetworkInterface::connectionTimeoutSeconds = 30;
