/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractWSServer.h"
#include "../Common/NotifyIcon.h"
#include "../Common/launchBrowser.h"


class WebTrackerOnOffConn : public AbstractWSServer {
public:
	std::function<void()> connectClicked;
	WebTrackerOnOffConn(NotifyIcon &icon, NetworkInterface &netAPI, aushort port, const char *resourceURI, const char *wsProtocol)
		: AbstractWSServer(netAPI, port, resourceURI, wsProtocol), menu(icon) {
		miON = miOFF = miCONN = 0;
	}
	void SetMessages(const wchar_t *enable, const wchar_t *connect, const wchar_t *disable) {
		miOFF = menu.AddMenuItem(disable, [this]() { DisableWeb(); }, false);
		miCONN = menu.AddMenuItem(connect, [this]() { if(connectClicked) connectClicked(); }, false);
		miON = menu.AddMenuItem(enable, [this]() { EnableWeb(); });
	}

private:
	NotifyIcon &menu;
	auint miON, miOFF, miCONN;

	void EnableWeb() {
		if(AreYouListening() == false) {
			Listen();
			menu.SetMenuItemStatus(miON, false);
			menu.SetMenuItemStatus(miCONN, true);
			menu.SetMenuItemStatus(miOFF, true);

		}
		else if(AreYouClosing()) {
			menu.ShowMessage(L"Previous server is still shutting down.\nTry again later."); //!< \todo warning here
		}
	}

	void DisableWeb() {
		if(AreYouListening()) {
			if(AreYouClosing() == false) {
				BeginClose();
				// turned on at shutdown completed, CloseCompleted callback
				//menu.SetMenuItemStatus(miON, true);
				menu.SetMenuItemStatus(miCONN, false);
				menu.SetMenuItemStatus(miOFF, false);
			}
		}
		else menu.ShowMessage(L"Server already shut down."); // fairly unlikely, but still possible
	}
	void CloseCompleted() {
		menu.SetMenuItemStatus(miON, true);
	}
};


class WebMonitorTracker : public WebTrackerOnOffConn {
public:
	WebMonitorTracker(NotifyIcon &icon, NetworkInterface &netAPI)
		: WebTrackerOnOffConn(icon, netAPI, 31000, "monitor", "M8M-monitor") {
	}
};


class WebAdminTracker : public WebTrackerOnOffConn {
public:
	WebAdminTracker(NotifyIcon &icon, NetworkInterface &netAPI)
		: WebTrackerOnOffConn(icon, netAPI, 31001, "admin", "M8M-admin") {
	}
};
