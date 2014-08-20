/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "LaunchBrowser.h"


void LaunchBrowser(const wchar_t *what) {
	if(!what) { // not supposed to happen but worth a check.
#if defined(_DEBUG)
		throw std::exception("nullptr passed to LaunchBrowser.");
#else
		return;
#endif
	}

#if defined(_WIN32)
	SHELLEXECUTEINFO exe;
	memset(&exe, 0, sizeof(exe));
	exe.cbSize = sizeof(exe);
	exe.fMask = SEE_MASK_NOASYNC;// | SEE_MASK_CLASSNAME;
	exe.lpFile = what;
	exe.nShow = SW_SHOWNORMAL;
	//exe.lpClass = L"http";
	ShellExecuteEx(&exe);	
	if(reinterpret_cast<int>(exe.hInstApp) < 32) throw std::exception("Could not run browser.");
#else
#error what to do here?
#endif
}
