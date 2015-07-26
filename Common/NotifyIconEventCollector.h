/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once


#include "AREN/ArenDataTypes.h"
#include "AREN/ScopedFuncCall.h"
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <vector>
#include "NotifyIconStructs.h"
#include "AbstractNotifyIcon.h"


/*! Implements an interface to manage a small control in OS notification area whose only task is to spawn
occasional messages and a popup menu on request.
M8M real UI is supposed to be web based and those commands basically enable/disable web control.
It does much more than collecting events in fact as it also takes care of estabilishing thread communication. */
template<typename AsyncUI>
class NotifyIconEventCollector : public AbstractNotifyIcon {
public:
	NotifyIconEventCollector() {
		lazyui.reset(new std::thread(os.GetUIManglingThreadFunc(sharedState, mutex)));
	}
	virtual ~NotifyIconEventCollector() {
		{
			std::unique_lock<std::mutex> lock(mutex);
			sharedState.terminate = true;
		}
		os.WakeupSignal();
		lazyui->join();
	}
	void SetIcon(const aubyte *rgbaPixels, asizei width, asizei height) {
		if(!rgbaPixels || !width || !height) throw std::exception("An icon must always be specified.");
		const asizei bytes = width * height * 4;
		std::unique_ptr<aubyte[]> newPixels(new aubyte[bytes]);
		memcpy_s(newPixels.get(), width * height * 4, rgbaPixels, width * height * 4);

		{
			std::unique_lock<std::mutex> lock(mutex);
			sharedState.icon.rgbaPixels = std::move(newPixels);
			sharedState.icon.width = width;
			sharedState.icon.height = height;
			sharedState.updateIcon = true;
		}
		os.WakeupSignal();
	}
	void SetCaption(const wchar_t *title) {
		std::wstring newTitle;
		if(title) newTitle = title;
		{
			std::unique_lock<std::mutex> lock(mutex);
			sharedState.icon.title = std::move(newTitle);
			sharedState.updateCaption = true;
		}
		os.WakeupSignal();
	}

	void ShowMessage(const wchar_t *text) {
		if(!text || wcslen(text) == 0) return;
		std::wstring msg;
		if(text) msg = text;

		{
			std::unique_lock<std::mutex> lock(mutex);
			sharedState.lastMessage.text = std::move(msg);
			sharedState.updateMessage = true;
		}
		os.WakeupSignal();
	}
	/*! If you're not using a menu, this does nothing. Otherwise, it dispatches at least one callback function according to menu callbacks.
	If multiple menu commands are available (say you slept this thread too long) then they are dispatched in the order of creation.
	Note I cannot run the callbacks right away as they might call back stuff which is locked so I must use a two-passes approach. */
	void Tick() {
		{
			{
				std::unique_lock<std::mutex> lock(mutex);
				dispatch.reserve(sharedState.commands.size());
				for(asizei loop = 0; loop < sharedState.commands.size(); loop++) {
					MenuItem &el(sharedState.commands[loop]);
					if(el.trigger) {
						dispatch.push_back(el.callback);
						el.trigger = false;
					}
				}
			}
			for(asizei loop = 0; loop < dispatch.size(); loop++) dispatch[loop]();
			dispatch.clear();
		}
		os.WakeupSignal();
	}
	void BuildMenu() {
		{
			std::unique_lock<std::mutex> lock(mutex);
			sharedState.commands = std::move(building);
			sharedState.regenMenu = true;
		}
		os.WakeupSignal();
	}
	auint AddMenuItem(const wchar_t *msg, std::function<void()> onClick, bool enabled = true) {
		if(!msg || !wcslen(msg)) throw std::exception("Menu items must always have some text.");
		MenuItem add;
		add.callback = onClick;
		add.message = msg;
		building.push_back(std::move(add));
		building.back().enabled = enabled;
		return auint(building.size() - 1);
	}
    void ChangeMenuItem(asizei entry, const wchar_t *msg, std::function<void()> onClick, bool enabled = true) {
        std::unique_lock<std::mutex> lock(mutex);
        sharedState.commands[entry].message = msg;
        sharedState.commands[entry].callback = onClick;
        sharedState.commands[entry].enabled = true;
        sharedState.commands[entry].trigger = false;
        sharedState.regenMenu = true;
    }
	bool GetMenuItemStatus(auint i) {
		std::unique_lock<std::mutex> lock(mutex);
        sharedState.regenMenu = true;
		return sharedState.commands[i].enabled;
	}
	void SetMenuItemStatus(auint i, bool enable) {
		{
			std::unique_lock<std::mutex> lock(mutex);
			sharedState.commandChanges.push_back(MenuItemEvent(i, enable? MenuItemEvent::esc_enabled : MenuItemEvent::esc_disabled));
		}
		os.WakeupSignal();
	}
	asizei AddMenuSeparator() {
		building.push_back(MenuItem(mit_separator));
		return building.size() - 1;
	}

private:
	AsyncUI os;

	std::unique_ptr<std::thread> lazyui;
	NotifyIconThreadShare sharedState;
	std::mutex mutex;

	std::vector<MenuItem> building;

	std::vector< std::function<void()> > dispatch;
};
