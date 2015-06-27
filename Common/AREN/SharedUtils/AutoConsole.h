/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
//#include "../../AREN-base/arenDataTypes.h"
#include "../../AREN/arenDataTypes.h"

#ifdef _WIN32
#include <WinSock2.h>
//^ due to windows headers being stupid, including windows.h before this (in case it's used) breaks compile.
// defining WIN32_LEAN_AND_MEAN can give problems with GDI+ if used so basically always include this just in case
#include <Windows.h> // AllocConsole and FreeConsole
#else
#error TODO
#endif

#include <fstream>
#include <iostream>
#include "../../AREN/ScopedFuncCall.h"

namespace sharedUtils {
namespace system {


template<bool INPUT_ENABLED>
class AutoConsole {
#ifdef _WIN32
	std::streambuf *prevOut, *prevErr, *prevIn;
	std::ofstream workOut, workErr;
	std::ifstream workIn;
#endif


public:
#ifdef _WIN32
	static bool IsInputEnabled() { return INPUT_ENABLED; }
	/*! Ok, you need a console. Ok. In 2014.
	I assume this is for debug or really really really important output of some stuff. Output I can understand, but input... just say NO.
	What happens if you don't enable input? std::cin will return 0xc for all read octects under windows instantaneously... so you get all sort of silly stuff
	and defeat the purpose. */
	explicit AutoConsole() : prevOut(nullptr), prevErr(nullptr), prevIn(nullptr) {
		if(!AllocConsole()) throw "Failed to access console!";
	}
    //! Specify 0xffffffff for parent process.
    AutoConsole(auint owner) {
        if(owner == auint(~0)) owner = ATTACH_PARENT_PROCESS;
        if(!AttachConsole(owner)) throw "Failed to attach console!";
    }
	~AutoConsole() { 
		if(prevIn) std::cin.rdbuf(prevIn);
		if(prevErr) std::cerr.rdbuf(prevErr);
		if(prevOut) std::cout.rdbuf(prevOut);
		FreeConsole();
	}
	void Enable() {
		// Can Windows not be a dumbwit and just do its magic when we attach to a console? After all, the above creates it and attaches us to it.
		// No, it cannot. We have to redirect our streams.
		workOut.open("CONOUT$");
		workErr.open("CONOUT$");
		if(INPUT_ENABLED) workIn.open("CONIN$");
		prevOut = std::cout.rdbuf(workOut.rdbuf());
		prevErr = std::cerr.rdbuf(workErr.rdbuf());
		if(INPUT_ENABLED) prevIn = std::cin.rdbuf(workIn.rdbuf());
	}
#endif
};


}
}
