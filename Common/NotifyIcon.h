/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#if defined(_WIN32)
#else
#error Needs some care.
#endif


#include "NotifyIconEventCollector.h"


class DummyNotifyIcon : public AbstractNotifyIcon {
public:
    void SetIcon(const aubyte *rgbaPixels, asizei width, asizei height) { }
	void SetCaption(const wchar_t *title) { }
	void ShowMessage(const wchar_t *text) { }
    void Tick() { }
	void BuildMenu() { }
	auint AddMenuItem(const wchar_t *msg, std::function<void()> onClick, bool status = true) {
        enabled.push_back(status);
		return auint(enabled.size() - 1);
    }
    bool GetMenuItemStatus(auint i) { return enabled[i]; }
	void SetMenuItemStatus(auint i, bool enable) { }
	asizei AddMenuSeparator() {
        enabled.push_back(true); // does not make much sense but it's the easier thing.
		return auint(enabled.size() - 1);
    }
private:
    std::vector<bool> enabled;
};


#if _WIN32
#include "Windows/AsyncNotifyIconPumper.h"
typedef NotifyIconEventCollector<windows::AsyncNotifyIconPumper> NotifyIcon;
#endif
