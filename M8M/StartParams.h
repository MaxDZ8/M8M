/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/AREN/SharedUtils/AutoCommandLine.h"


//! Parameters passed from command line.
struct StartParams {
    bool secondaryInstance = false;
    bool allocConsole = false;
    std::wstring cfgDir;
    std::wstring cfgFile;

private:
    sharedUtils::system::AutoCommandLine cmdline;

    //! Not very efficient to scan multiple times but it gives me easy interface. Double pointers + const = ouch, but easy
    bool ParseParam(std::wstring &value, const asizei argc, wchar_t **argv, const wchar_t *name, const wchar_t *defaultValue) {
	    bool found = false;
	    for(asizei loop = 0; loop < argc; loop++) {
		    if(wcsncmp(argv[loop], L"--", 2) == 0 && wcscmp(argv[loop] + 2, name) == 0) {
			    found = true;
			    value.clear();
			    if(loop + 1 < argc) {
				    loop++;
				    asizei limit;
				    for(limit = loop; limit < argc; limit++) {
					    if(wcsncmp(argv[loop], L"--", 2) == 0) break;
				    }
				    for(; loop < limit; loop++) {
					    if(value.length()) value += L" ";
					    value += argv[loop];
				    }
			    }
			    break;
		    }
	    }
	    if(!found) value = defaultValue;
	    return found;
    }
};