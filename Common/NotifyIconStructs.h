/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <vector>
#include <memory>
#include <functional>
#include "AREN/ArenDataTypes.h"

enum MenuItemType {
	mit_command,
	mit_separator
};

struct MenuItem {
	std::wstring message;
	bool trigger;
	std::function<void()> callback;
	bool enabled;
	MenuItemType type;
	MenuItem(MenuItemType t = mit_command) : trigger(false), type(t), enabled(true) { }
};


struct MenuItemEvent {
	enum EnableStateChange {
		esc_notChanged,
		esc_enabled,
		esc_disabled
	};
	asizei controlIndex;
	EnableStateChange statusChange;
	MenuItemEvent(asizei index, EnableStateChange newState) : controlIndex(index), statusChange(newState) { }
};


struct Icon {
	std::unique_ptr<aubyte[]> rgbaPixels;
	asizei width, height;
	std::wstring title;
	Icon() : width(0), height(0) { }
};

struct Message {
	std::wstring text;
};

//! Used to hold/pass messages from main to gui thread or vice versa.
struct NotifyIconThreadShare {
    // main -> gui thread
	bool terminate, regenMenu, updateIcon, updateMessage;
	std::vector<MenuItem> commands;
	std::vector<MenuItemEvent> commandChanges;
	Message lastMessage;
	Icon icon;

	// gui thread -> main
	bool guiTerminated;

	explicit NotifyIconThreadShare() : terminate(false), regenMenu(false), updateIcon(false), updateMessage(false), guiTerminated(false) { }
};
