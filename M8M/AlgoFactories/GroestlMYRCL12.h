/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../OpenCL12Wrapper.h"
#include "../AlgoFamily.h"
#include "implementations/GroestlMyrMultistepOpenCL12.h"


class GroestlMYRCL12 : public AlgoFamily<OpenCL12Wrapper> {
	GroestlMYRMultistepOpenCL12 multiStep;

public:
	explicit GroestlMYRCL12(bool profiling, OpenCL12Wrapper::ErrorFunc f) : AlgoFamily("groestl-MYR"), multiStep(profiling, f) {
		implementations.push_back(&multiStep);
	}
};
