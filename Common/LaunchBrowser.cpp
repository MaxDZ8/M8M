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


/*! \todo Same as LaunchBrowser... but with different error messages. I should really change this.
Since those are usually "fire and forget" operations, maybe I could have a set of objects inheriting from a base class
and calling more or less the same thing so for example I could have...

	SysBrowserLauncher(url); // temporary object instantiated and blasted away in a single statement
	SysFileExplorerFolder(where);

Another problem is that there's no validation on what's passed in there. It could be a file and the result wouldn't be
what this is supposed to do. In fact, there's no guarantee LaunchBrowser does what it's supposed to do either!
*/	
void OpenFileExplorer(const wchar_t *folder) {
	if(!folder) { // not supposed to happen but worth a check.
#if defined(_DEBUG)
		throw std::exception("nullptr passed to OpenFileExplorer.");
#else
		return;
#endif
	}

#if defined(_WIN32)
	SHELLEXECUTEINFO exe;
	memset(&exe, 0, sizeof(exe));
	exe.cbSize = sizeof(exe);
	exe.fMask = SEE_MASK_NOASYNC;// | SEE_MASK_CLASSNAME;
	exe.lpFile = folder;
	exe.nShow = SW_SHOWNORMAL;
	//exe.lpClass = L"http";
	ShellExecuteEx(&exe);	
	if(reinterpret_cast<int>(exe.hInstApp) < 32) throw std::exception("Could not run file explorer!");
#else
#error what to do here?
#endif
}
