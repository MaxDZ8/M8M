/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../OpenCL12Wrapper.h"
#include "../AlgoFamily.h"
#include "implementations/NeoScryptMultistepOpenCL12.h"


class NeoScryptCL12 : public AlgoFamily<OpenCL12Wrapper> {
	NeoScryptMultistepOpenCL12 multiStep;

public:
	explicit NeoScryptCL12(bool profiling, OpenCL12Wrapper::ErrorFunc f) : AlgoFamily("neoScrypt"), multiStep(profiling, f) {
		implementations.push_back(&multiStep);
	}
};
