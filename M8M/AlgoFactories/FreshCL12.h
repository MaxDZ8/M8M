/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../OpenCL12Wrapper.h"
#include "../AlgoFamily.h"
#include "implementations/FreshMultistepOpenCL12.h"


class FreshCL12 : public AlgoFamily<OpenCL12Wrapper> {
	FreshMultistepOpenCL12 multiStep;

public:
	FreshCL12(bool profiling, OpenCL12Wrapper::ErrorFunc f) : AlgoFamily("fresh"), multiStep(profiling, f) {
		implementations.push_back(&multiStep);
	}
};
