/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#if defined(_WIN32)
#include "AsyncNotifyIconPumper.h"

namespace windows {

const UINT AsyncNotifyIconPumper::WM_APP_DATA_CHANGED       = WM_APP + 0;
const UINT AsyncNotifyIconPumper::WM_APP_NOTIFICON = WM_APP + 1;
UINT AsyncNotifyIconPumper::totalIcons = 1;

std::function<void()> AsyncNotifyIconPumper::GetUIManglingThreadFunc(NotifyIconThreadShare &shared, std::mutex &mutex) {
    this->shared = &shared;
    this->mutex = &mutex;
	this->iconIndex = totalIcons++;

    return [this]() {
        using namespace Gdiplus;
	    GdiplusStartupInput gdipstart;
        Gdiplus::GdiplusStartupOutput gdipInitResult;
        ULONG_PTR gdiplusToken;
	    Status res = GdiplusStartup(&gdiplusToken, &gdipstart, &gdipInitResult);
	    if(res != Ok) throw std::string("Could not init GDI+");
	    ScopedFuncCall cleargdi([gdiplusToken]() { GdiplusShutdown(gdiplusToken); });
		
		WNDCLASSEX winClass;
	    memset(&winClass, 0, sizeof(winClass));
	    winClass.cbSize = sizeof(winClass);
	    winClass.lpfnWndProc = WindowProcedure;
	    winClass.hInstance = GetModuleHandle(NULL);
	    winClass.lpszClassName = L"M8M window class for notify pump.";
		ATOM registered = RegisterClassEx(&winClass);
		if(!registered) throw std::exception("Could not register window class.");
		ScopedFuncCall clearClass([&winClass]() { UnregisterClass(winClass.lpszClassName, winClass.hInstance); });

        ScopedFuncCall onTermination([this]() {
			std::unique_lock<std::mutex> lock(*this->mutex);
			if(this->asyncOwned.contextMenu) {
				DestroyMenu(this->asyncOwned.contextMenu);
				this->asyncOwned.contextMenu = 0;
			}
			if(this->asyncOwned.osIcon) { // icon is a function of bitmap so if bitmap goes, the icon will get invalid
				if(this->asyncOwned.removeFromNotificationArea) {
					NOTIFYICONDATA iid;
					memset(&iid, 0, sizeof(iid));
					iid.cbSize = sizeof(iid);
					iid.hWnd = asyncOwned.windowHandle;
					iid.uID = iconIndex;
					BOOL ret = Shell_NotifyIcon(NIM_DELETE, &iid);
				}
				DestroyIcon(this->asyncOwned.osIcon);
				this->asyncOwned.osIcon = 0;
			}
			if(this->asyncOwned.windowHandle) {
				DestroyWindow(this->asyncOwned.windowHandle);
				this->asyncOwned.windowHandle = 0;
			}
			if(this->asyncOwned.iconGraphics) delete this->asyncOwned.iconGraphics; // this must happen before GDI+ shutdown or it will be considered an incomplete type
	        if(this->shared) this->shared->guiTerminated = true;
		});
        try {
	        const DWORD style = 0;
	        const DWORD exStyle = 0;
	        const int x = 100;
	        const int y = 100;
	        const int w = 168;
	        const int h = 105;
	        HWND windowHandle = CreateWindowEx(exStyle, winClass.lpszClassName, L"M8M window for message pumping from notify area", style, x, y, w, h, NULL, NULL, GetModuleHandle(NULL), this);
	        if(windowHandle == NULL) throw std::string("Could not create window for message pumping.");
			ScopedFuncCall dwin([windowHandle]() { DestroyWindow(windowHandle); });

			{
				std::unique_lock<std::mutex> lock(*this->mutex);
				this->asyncOwned.windowHandle = windowHandle;
				dwin.Dont();
			}
			PostMessage(windowHandle, WM_APP_DATA_CHANGED, 0, 0); // apparently having something in the queue allows this thread to be scheduled more easily (not surprisingly)
            BOOL go;
            MSG message;
            while(go = GetMessage(&message, windowHandle, 0, 0)) {
                if(go == -1) break; // additional case (function destroyed but still pumping), WM_QUIT returns 0, otherwise message
				{
					std::unique_lock<std::mutex> lock(*this->mutex);
					if(this->shared->terminate) break;
				}
                TranslateMessage(&message);
			    DispatchMessage(&message);
			}

		} catch(...) {
		}
	};
}



LRESULT CALLBACK AsyncNotifyIconPumper::WindowProcedure(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
	/* This will receive a WM_CREATE as soon as the window is created, before CreateWindowEx returns.
	This will point the owner to a valid object passed as WM_CREATE data. The windowProc is called from inside DispatchMessage.
	Filter out application messages first! */
	static AsyncNotifyIconPumper *owner = nullptr;
	try {
		bool redrawMenu = false;
		switch(msg) {
			case WM_CREATE: // 0x0001, fourth message after creation
			case WM_NCCREATE: // 0x0081, 129dec, second message sent after creation
				owner = reinterpret_cast<AsyncNotifyIconPumper*>(((LPCREATESTRUCT)lparam)->lpCreateParams);
				return TRUE; // continue creating the window.
			case WM_COMMAND:
				break;
			case WM_APP_DATA_CHANGED: {
				std::unique_lock<std::mutex> lock(*owner->mutex);
				if(owner->shared->terminate) {
					PostQuitMessage(0);
					return 0;
				}
				if(owner->shared->updateIcon || owner->shared->updateCaption) {
					owner->UpdateIconNCaption();
					owner->shared->updateIcon = owner->shared->updateCaption = false;
				}
				if(owner->shared->regenMenu) {
					HMENU newMenu = owner->GenMenu();
					if(owner->asyncOwned.contextMenu) DestroyMenu(owner->asyncOwned.contextMenu);
					SetMenu(owner->asyncOwned.windowHandle, newMenu);
					owner->asyncOwned.contextMenu = newMenu;
					owner->shared->regenMenu = false;
				}
				for(asizei loop = 0; loop < owner->shared->commandChanges.size(); loop++) {
					const asizei slot = owner->shared->commandChanges[loop].controlIndex;
					if(slot >= owner->shared->commands.size()) continue;
					owner->Update(owner->shared->commands[slot], owner->shared->commandChanges[loop]);
				}
				redrawMenu = owner->shared->commandChanges.size() > 0;
				owner->shared->commandChanges.clear();
				if(owner->shared->updateMessage) {
					owner->UpdateMessage();
					owner->shared->updateMessage = false;
				}
				break;
			}
			case WM_APP_NOTIFICON: return owner->NotifyCallback(LOWORD(lparam), GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam));
			default:
				return DefWindowProc(window, msg, wparam, lparam); 
		}
		if(redrawMenu) DrawMenuBar(owner->asyncOwned.windowHandle);
	} catch(...) {
		// just suppress exceptions!
	}
	return 0;
}


LRESULT AsyncNotifyIconPumper::NotifyCallback(WORD msg, int x, int y) {
    switch(msg) {
	case WM_CONTEXTMENU: {
		{
			std::unique_lock<std::mutex> lock(*mutex);
			if(!asyncOwned.contextMenu) return 0;
		}
	    UINT flags = TPM_CENTERALIGN | TPM_BOTTOMALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON;
		flags |= TPM_RETURNCMD; // this is the easiest way to pull out the command index.
	    SetForegroundWindow(asyncOwned.windowHandle);
        BOOL dirty = TrackPopupMenu(asyncOwned.contextMenu, flags, x, y, 0, asyncOwned.windowHandle, NULL);
		if(dirty > 0) { // otherwise user did not click, not supposed to be negative
			dirty--;
			std::unique_lock<std::mutex> lock(*mutex);
			if(asizei(dirty) >= shared->commands.size()) throw std::exception("Command list incoherent?");
			this->shared->commands[dirty].trigger = true;
		}
	    PostMessage(asyncOwned.windowHandle, WM_NULL, 0, 0);
	}
	case WM_COMMAND: {
		break;
	}
	//default:
	//std::cout<<std::hex<<msg<<std::dec<<std::endl;
	}
	return 1;
}


HMENU AsyncNotifyIconPumper::GenMenu() {
    HMENU build = CreatePopupMenu();
	ScopedFuncCall clearBuild([build]() { DestroyMenu(build); });
    for(asizei loop = 0; loop < shared->commands.size(); loop++) {
        const MenuItem &mi(shared->commands[loop]);
		MENUITEMINFO desc;
        memset(&desc, 0, sizeof(desc));
		desc.cbSize = sizeof(desc);
		desc.fMask = MIIM_FTYPE;
		desc.fType = TO_MFT(mi.type);
        switch(mi.type) {
		case mit_command: {
			desc.fMask |= MIIM_STRING | MIIM_ID | MIIM_STATE;
			desc.wID = UINT(loop + 1); // 0 reserved.
			desc.dwTypeData = const_cast<wchar_t*>(mi.message.c_str());
			desc.fState = mi.enabled? MFS_ENABLED : MFS_DISABLED;
            break;
	    }
		}
        BOOL success = InsertMenuItem(build, UINT(loop), TRUE, &desc);
        if(success == FALSE) throw std::exception("Could not add command to menu.");
	}
    clearBuild.Dont();
    return build;
}


void AsyncNotifyIconPumper::UpdateMessage() {
	NOTIFYICONDATA idata;
	memset(&idata, 0, sizeof(idata));
	idata.cbSize = sizeof(idata);
	idata.hWnd = asyncOwned.windowHandle;
	idata.uID = iconIndex;
	idata.uFlags = NIF_INFO | NIF_SHOWTIP;
	idata.dwInfoFlags = 0;
	memset(idata.szInfo, 0, sizeof(idata.szInfo));
    const std::wstring &text(shared->lastMessage.text);
	asizei len = 200 < text.length()? 200 : text.length();
	for(asizei cp = 0; cp < len; cp++) idata.szInfo[cp] = text[cp];
	const wchar_t *title = L"M8M";
	memset(idata.szInfoTitle, 0, sizeof(idata.szInfoTitle));
	for(asizei cp = 0; title[cp]; cp++) idata.szInfoTitle[cp] = title[cp];
	idata.uTimeout = 1000; // deprecated since win7, I set it to a random value.
	//!< \todo what if this is now win7?
	Shell_NotifyIcon(NIM_MODIFY, &idata);
}


void AsyncNotifyIconPumper::UpdateIconNCaption() {
	// prepare new icon first, it's a bit more complicated.
	int words[64 * 64];
	int w = 0, h = 0;
	if(shared->updateIcon) {
		if(shared->icon.width > 64 || shared->icon.height > 64) throw std::string("Icon max width is 64x64.");
		w = int(shared->icon.width);
		h = int(shared->icon.height);
		memcpy_s(words, sizeof(words), shared->icon.rgbaPixels.get(), w * h * 4); //!< \todo CreateBitmap needs scanlines to be WORD aligned, and this is not currently guaranteed for tight packing, ok for now.
		shared->icon.rgbaPixels.reset();
	}

	using namespace Gdiplus;
	std::unique_ptr<Bitmap> newBitmap;
	HICON newIcon = 0, prevIcon = 0;
	Bitmap *prevBitmap = nullptr;
	if(shared->updateIcon) {
		newBitmap.reset(new Bitmap(w, h, 4 * w, PixelFormat32bppARGB, reinterpret_cast<BYTE*>(words)));
		Status result = newBitmap->GetHICON(&newIcon);
		if(result != Ok) throw std::string("GetHICON failed, error ") + std::to_string(result);
		prevIcon = asyncOwned.osIcon;
		prevBitmap = asyncOwned.iconGraphics;
		asyncOwned.iconGraphics = newBitmap.release();
		asyncOwned.osIcon = newIcon;
	}
	ScopedFuncCall clearPrev([prevIcon, prevBitmap]() {
		if(prevIcon) DestroyIcon(prevIcon);
		if(prevBitmap) delete prevBitmap;
	});

	const DWORD ACTION = prevIcon? NIM_MODIFY : NIM_ADD;
	NOTIFYICONDATA idata;
	memset(&idata, 0, sizeof(idata));
	idata.cbSize = sizeof(NOTIFYICONDATA);
	idata.hWnd = asyncOwned.windowHandle;;
	idata.uID = this->iconIndex;;
	idata.uFlags = NIF_TIP | NIF_SHOWTIP;  // NIF_SHOWTIP vista and later
	idata.uCallbackMessage = WM_APP_NOTIFICON;
	if(shared->updateIcon) {
		idata.hIcon = asyncOwned.osIcon; //! \todo http://msdn.microsoft.com/en-us/library/windows/desktop/bb773352(v=vs.85).aspx, LoadIconMetric
		idata.uFlags |= NIF_ICON;
	}
	if(shared->icon.title.size()) { // always needs to be put back in place
		std::wstring &title(shared->icon.title);
		asizei len = title.length() < 127? title.length() : 127;
		for(asizei cp = 0; cp < len; cp++) idata.szTip[cp] = title[cp];
		idata.uFlags |= NIF_MESSAGE;
	}
	BOOL success = Shell_NotifyIcon(ACTION, &idata);
	if(!success) throw std::exception("Could not add/update icon in notification area.");

	if(!prevIcon) {
		idata.uFlags |= NIM_SETVERSION;
		idata.uVersion = NOTIFYICON_VERSION_4;
		success = Shell_NotifyIcon(NIM_SETVERSION, &idata);
		if(!success) throw std::exception("Could not set icon version.");
	}
	asyncOwned.removeFromNotificationArea = true;
}


void AsyncNotifyIconPumper::Update(MenuItem &mi, const MenuItemEvent &mod) {
	MENUITEMINFO change;
	memset(&change, 0, sizeof(change));
	change.cbSize = sizeof(change);

	if(mod.statusChange != mod.esc_notChanged) {
		change.fMask |= MIIM_STATE;
		change.fState = mod.statusChange == mod.esc_disabled? MFS_DISABLED : MFS_ENABLED;
	}
	BOOL success = SetMenuItemInfo(asyncOwned.contextMenu, mod.controlIndex + 1, FALSE, &change);
	if(success) {
		if(mod.statusChange != mod.esc_notChanged) mi.enabled = mod.statusChange != mod.esc_disabled;
	}
}

}

#endif
