/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <string>
#include <functional>

namespace commands {

/*! \todo This should very likely be somewhere else and not in the commands namespace but I feel like this would be a bike shed discussion.

Note there's no extension token here. Those are expected to be put in a map or somehow associated at higher level. */
struct ExtensionState {
	std::string desc; //!< human readable presentation string, if empty then extension is not enumerated in list
	bool disabled; //!< if false, don't call enable again. Set by UpgradeCMD after enable has returned.
	std::function<void()> enable;
};

}
