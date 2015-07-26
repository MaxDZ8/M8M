/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once

#include "../arenDataTypes.h"

#if defined(_WIN32)
#include <Windows.h>
#else
#error Check out unique checking!
#endif

/*! \brief Creates an OS-dependant system-wide object to avoid running multiple instances of this program. */
class OSUniqueChecker {
#if _WIN32
	/* A unique mutex to be named. This technique is noted in PSDK WinMain function documentation it is noted that
	"... a malicious user may ... prevent your application from starting..."
	however, the required machinery to avoid this issue doesn't seem worth it.
	Perhaps it would be just a better idea to use a temporary file in user's %APPDATA */
	HANDLE mutex;
#else
#error "uniqueChecker does not support current compile target!"
#endif

	bool firstInstance;

public:
	OSUniqueChecker();
	~OSUniqueChecker();

	//! I'm initializing, can I go ahead? \returns false if the caller is suggested to give up.
	bool CanStart(const wchar_t *name);
};
