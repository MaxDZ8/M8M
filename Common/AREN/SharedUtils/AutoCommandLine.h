/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
//#include "../../AREN-base/arenDataTypes.h"
#include "../../AREN/arenDataTypes.h"
#include "../../AREN/ScopedFuncCall.h"

#ifdef _WIN32
#include <Windows.h> // GetCommandLine and CommandLineToArgvW
#else
#error TODO
#endif

namespace sharedUtils {
namespace system {

#ifdef _WIN32
class AutoCommandLine {
public:
	wchar_t **argv;
	int argc;

	AutoCommandLine() : argv(nullptr), argc(0) {
		int iargc;
		argv = CommandLineToArgvW(GetCommandLineW(), &iargc);
		if(argv == nullptr) throw new std::exception("could not get command line params.");
		argc = asizei(iargc);
	}
	~AutoCommandLine() {
		LocalFree(argv);
	}
};
#else 
#error Target os not supported in AutoCommandLine.h!
class AutoCommandLine { };
#endif

}
}
