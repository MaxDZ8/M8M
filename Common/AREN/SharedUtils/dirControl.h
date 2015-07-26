/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once


//#include "../../AREN-base/arenDataTypes.h"
#include "../ArenDataTypes.h"
#include <memory.h>
#include <string.h>

#if defined(_WIN32)
#include <WinSock2.h> // not really necessary, but easier than solve the ugly mess that are windows headers
#include <Windows.h>
#else
#error Look into dirControl.h for OS support
#endif

namespace sharedUtils {
namespace system {


static void GetCurrentDir(std::unique_ptr<wchar_t[]> &current) {
#if defined(_WIN32)
	// For first, ask the win32 function for dir length.
	DWORD chars = GetCurrentDirectoryW(0, NULL); // count including teminator char
	if(chars) {
		current.reset(new wchar_t[chars]);
		const DWORD copied = GetCurrentDirectoryW(chars, current.get());
		if(copied != chars - 1) {	// won't return the counting terminating null char
			current.reset();		// In some ill-specified cases, will return a different count w/o failing. I don't care for those cases!
			return;
		}
	}
#else
#error Look into dirControl.h for OS support GetCurrentDir
#endif
}


/*! Saves the current directory (as complete path), changes directory to the dir you want.
On destruction, changes back to the original directory so later code can safely assume the cwd
was not changed. */
template<bool PUSH_CWD>
class AutoGoDir {
public:
	AutoGoDir() : changed(false) {
		if(PUSH_CWD) GetCurrentDir(prev);
	}
	AutoGoDir(const wchar_t *go, bool createOnNeed = false) : changed(false) {
		if(PUSH_CWD) GetCurrentDir(prev);
		if(!GoDir(go, createOnNeed)) {
			if(PUSH_CWD) GoDir(prev.get());
		}
		else changed = true;
	}
	~AutoGoDir() { if(PUSH_CWD) GoDir(prev.get()); }
	bool Changed() const { return changed; }
	static bool GoDir(const wchar_t *dir, bool createOnNeed = false) {
		if(!dir) return true;
		const asizei len = wcslen(dir);
		if(!len) return true;

		#ifdef _WIN32
		auto cd = [](const wchar_t *cwd) -> bool {
			const asizei mplen = MAX_PATH - 3; // 1 extra for extra safety
			const asizei len = wcslen(cwd);
			if(len < mplen) {
				if(!SetCurrentDirectoryW(cwd)) return false;
			}
			else { // maybe I'd have to do that fully recorsive?
				asizei match = mplen;
				while(match >= len && (cwd[match] != '\\' || cwd[match] != '/')) match--;
				std::unique_ptr<wchar_t[]> wtf(new wchar_t[len + 1]);
				std::unique_ptr<wchar_t[]> currently;
				GetCurrentDir(currently);
				wcscpy_s(wtf.get(), len + 1, cwd);
				wtf[match] = 0;
				if(!SetCurrentDirectoryW(wtf.get())) return false;
				if(!SetCurrentDirectoryW(wtf.get() + match + 1)) {
					SetCurrentDirectoryW(currently.get());
					return false;
				}
			}
			return true;
		};
		bool success = false;

		if(cd(dir)) return true;
		else if(createOnNeed) {
			// CreateDirectory allows me to go much longer, 32k chars, if I concatenate some shit.
			// It seems like this must be absolute (?)
            // If this path is not absolute, pull out CWD. I consider a path absolute if it has : before a separator.
			std::wstring appended(L"\\\\?\\");
            asizei separator = 0;
            while(dir[separator]) {
                if(dir[separator] == '\\' || dir[separator] == '/') break;
                separator++;
            }
            bool relative = true;
            if(dir[separator] == '\\' || dir[separator] == '/') {
                for(asizei check = 0; check < separator; check++) {
                    if(dir[check] == ':') {
                        relative = false;
                        break;
                    }
                }
            }
            if(relative) {
			    std::unique_ptr<wchar_t[]> cwd;
			    GetCurrentDir(cwd);
			    appended += cwd.get();
			    appended += L"\\";
            }
			appended += dir;
            BOOL result = CreateDirectoryW(appended.c_str(), NULL);
			DWORD fail = GetLastError();
			if(!result || !cd(dir)) return false;
		}
		#else
		#error Compile target not supported by directoryChanger_t::GoDirectory
		#endif
		return true;
	}

private:
	bool changed;
	std::unique_ptr<wchar_t[]> prev;
};


}
}
