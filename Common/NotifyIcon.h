/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#if defined(_WIN32)
#else
#error Needs some care.
#endif


#include "NotifyIconEventCollector.h"


#if _WIN32
#include "Windows/AsyncNotifyIconPumper.h"
typedef NotifyIconEventCollector<windows::AsyncNotifyIconPumper> NotifyIcon;
#endif
