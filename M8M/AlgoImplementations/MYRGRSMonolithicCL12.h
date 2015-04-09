/*
 * Copyright (C) 2015 Massimo Del Zotto
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractAlgorithm.h"

namespace algoImplementations {

class MYRGRSMonolithicCL12 : public AbstractAlgorithm {
public:
    MYRGRSMonolithicCL12(cl_context ctx, cl_device_id dev, asizei concurrency)
        : AbstractAlgorithm(concurrency, ctx, dev, "GRSMYR", "monolithic", "v1", 8) { }

    std::vector<std::string> Init(ConfigDesc *desc, AbstractSpecialValuesProvider &specials, const std::string &loadPathPrefix) {
        auint roundCount[5] = {
            14, 14, 14, // groestl rounds
            2, 3 // SHA rounds
        };
        ResourceRequest resources[] = {
            ResourceRequest("roundCount", CL_MEM_HOST_NO_ACCESS | CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(roundCount), roundCount)
        };
        resources[0].presentationName = "Round iterations";
        if(desc) return DescribeResources(*desc, resources, sizeof(resources) / sizeof(resources[0]), specials);
        auto errors(PrepareResources(resources, sizeof(resources) / sizeof(resources[0]), specials));
        if(errors.size()) return errors;

        typedef WorkGroupDimensionality WGD;
        KernelRequest kernels[] = {
            {
                "grsmyr_monolithic.cl", "grsmyr_monolithic", "",
                WGD(256),
                "$candidates, $wuData, $dispatchData, roundCount"
            }
        };
        return PrepareKernels(kernels, sizeof(kernels) / sizeof(kernels[0]), specials, loadPathPrefix);
    }
    bool BigEndian() const { return true; }
    aulong GetDifficultyNumerator() const { return 0x000000000000FFFFull; }
};

}
