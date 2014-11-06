/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../OpenCL12Wrapper.h"
#include "../AlgoFamily.h"
#include "implementations/grsmyrMonolithicOpenCL12.h"


class GRSMYRCL12 : public AlgoFamily<OpenCL12Wrapper> {
	GRSMYRMonolithicOpenCL12 multiStep;

public:
	explicit GRSMYRCL12(bool profiling, OpenCL12Wrapper::ErrorFunc f) : AlgoFamily("grsmyr"), multiStep(profiling, f) {
		implementations.push_back(&multiStep);
	}
};
