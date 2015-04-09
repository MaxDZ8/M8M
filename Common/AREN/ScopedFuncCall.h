/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <functional>

// This file is originally part of AREN, but no need to be so modular here.
//namespace sharedUtils {

/*! Ab-using lamda's capture lists to perform cleanup on exceptions. */
class ScopedFuncCall {
	std::function<void()> function;
	bool call;
public:
	ScopedFuncCall(std::function<void()> &func) : function(func) { call = true; }
	ScopedFuncCall(std::function<void()> func)  : function(func) { call = true; }
	void Dont() { call = false; }
	~ScopedFuncCall() { if(call) function(); }
};

//}
