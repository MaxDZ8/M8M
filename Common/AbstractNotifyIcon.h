/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once


#include "AREN/ArenDataTypes.h"
#include <functional>


/*! Implements an interface to manage a small control in OS notification area whose only task is to spawn
occasional messages and a popup menu on request. 
M8M real UI is supposed to be web based and those commands basically enable/disable web control.
It does much more than collecting events in fact as it also takes care of estabilishing thread communication. */
class AbstractNotifyIcon {
public:
    explicit AbstractNotifyIcon() { }
    virtual ~AbstractNotifyIcon() { }
	virtual void SetIcon(const aubyte *rgbaPixels, asizei width, asizei height) = 0;
	virtual void SetCaption(const wchar_t *title) = 0;
	virtual void ShowMessage(const wchar_t *text) = 0;
    virtual void Tick() = 0;
	virtual void BuildMenu() = 0;
	virtual auint AddMenuItem(const wchar_t *msg, std::function<void()> onClick, bool enabled = true) = 0;
	virtual bool GetMenuItemStatus(auint i) = 0;
	virtual void SetMenuItemStatus(auint i, bool enable) = 0;
	bool ToggleMenuItemStatus(auint i) { // would be an interface if not for that... oh well.
		bool status = GetMenuItemStatus(i);
		SetMenuItemStatus(i, !status);
		return !status;
    }
	virtual asizei AddMenuSeparator() = 0;
};
