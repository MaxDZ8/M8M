/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../OpenCL12Wrapper.h"
#include "../AlgoFamily.h"
#include "implementations/QubitMultistepOpenCL12.h"


class QubitCL12 : public AlgoFamily<OpenCL12Wrapper> {
	QubitMultistepOpenCL12 multiStep;

public:
	explicit QubitCL12(bool profiling, OpenCL12Wrapper::ErrorFunc f) : AlgoFamily("qubit"), multiStep(profiling, f) {
		implementations.push_back(&multiStep);
	}
};
