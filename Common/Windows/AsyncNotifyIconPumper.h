/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once

#if defined(_WIN32)
#include <WinSock2.h> // what is this? In general, I would just define WIN32_LEAN_AND_MEAN but looks like it breaks GDI+ (?)
#include <Windows.h>
#include <gdiplus.h>
#include <windowsx.h>
#include "../NotifyIconStructs.h"
#include "../AREN/ScopedFuncCall.h"
#include <mutex>
#include <thread>
#include <string>
#include <functional>

namespace windows {

class AsyncNotifyIconPumper {
public:
    void WakeupSignal() {
		std::unique_lock<std::mutex> lock(*mutex);
        if(asyncOwned.windowHandle) PostMessage(asyncOwned.windowHandle, WM_APP_DATA_CHANGED, 0, 0);
	}
    std::function<void()> GetUIManglingThreadFunc(NotifyIconThreadShare &s, std::mutex &mutex);

	AsyncNotifyIconPumper() : iconIndex(0), shared(nullptr), mutex(nullptr) { }

private:
    static const UINT WM_APP_DATA_CHANGED;
    static const UINT WM_APP_NOTIFICON;
    static UINT totalIconsCHECK;

    struct AsyncOwned { // the other thread spawns real OS resources here and kills them on exit.
		HWND windowHandle;
		HMENU contextMenu;
		Gdiplus::Bitmap *iconGraphics;
		HICON osIcon;
		bool removeFromNotificationArea;

		AsyncOwned() : windowHandle(0), contextMenu(0), osIcon(0), removeFromNotificationArea(false), iconGraphics(nullptr) { }
		// No dtor. Automatically cleared by the other thread!
	} asyncOwned;
	
    UINT iconIndex;
    NotifyIconThreadShare *shared;
    std::mutex *mutex;

    static UINT TO_MFT(MenuItemType t) {
        switch(t) {
		case mit_separator: return MFT_SEPARATOR;
		case mit_command: return MFT_STRING;
		}
        throw std::exception("Code out of sync, unknown MenuItemType.");
	}
    static LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
	LRESULT NotifyCallback(WORD msg, int x, int y);

    HMENU GenMenu();
    void UpdateMessage();
	void UpdateIconNCaption(); 
	void Update(MenuItem &mi, const MenuItemEvent &mod);
};

}

#endif
