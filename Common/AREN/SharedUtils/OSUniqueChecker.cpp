/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "OSUniqueChecker.h"


OSUniqueChecker::OSUniqueChecker() : firstInstance(false) {
#ifdef _WIN32
	mutex = NULL;
#else
#error "uniqueChecker_t::uniqueChecker_t does not support current compile target!";
#endif
}


OSUniqueChecker::~OSUniqueChecker() {
#ifdef _WIN32
	if(mutex != NULL) {
		ReleaseMutex(mutex);
		// ReleaseMutex tells other processes "i'm out of the mutex" (state), the handle must also be released
		CloseHandle(mutex);
	}
#else
#error "uniqueChecker_t::~uniqueChecker_t does not support current compile target!";
#endif
}


bool OSUniqueChecker::CanStart(const wchar_t *name) {
	if(mutex != NULL) return firstInstance;
	mutex = CreateMutex(NULL, TRUE, name);
	if(GetLastError() == ERROR_ALREADY_EXISTS || !mutex) firstInstance = false;
	else firstInstance = true;
	return firstInstance;
}
