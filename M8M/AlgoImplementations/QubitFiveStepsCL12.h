/*
 * Copyright (C) 2015 Massimo Del Zotto
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractAlgorithm.h"

namespace algoImplementations {

class QubitFiveStepsCL12 : public AbstractAlgorithm {
public:
    QubitFiveStepsCL12(cl_context ctx, cl_device_id dev, asizei concurrency)
        : AbstractAlgorithm(concurrency, ctx, dev, "Qubit", "fiveSteps", "v1", 16) { }

    std::vector<std::string> Init(ConfigDesc *desc, AbstractSpecialValuesProvider &specials, const std::string &loadPathPrefix) {
        const asizei passingBytes = this->hashCount * 16 * sizeof(cl_uint);
        KnownConstantProvider K; // all the constants are fairly nimble so I don't save nor optimize this in any way!
        auto AES_T_TABLES(K[CryptoConstant::AES_T]);
        auto SIMD_ALPHA(K[CryptoConstant::SIMD_alpha]);
        auto SIMD_BETA(K[CryptoConstant::SIMD_beta]);
        ResourceRequest resources[] = {
            ResourceRequest("io0", CL_MEM_HOST_NO_ACCESS, passingBytes),
            ResourceRequest("io1", CL_MEM_HOST_NO_ACCESS, passingBytes),
            ResourceRequest("AES_T_TABLES", CL_MEM_HOST_NO_ACCESS | CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, AES_T_TABLES.second, AES_T_TABLES.first),
            Immediate<cl_uint>("sh3_roundCount", 14),
            ResourceRequest("SIMD_ALPHA", CL_MEM_HOST_NO_ACCESS | CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, SIMD_ALPHA.second, SIMD_ALPHA.first),
            ResourceRequest("SIMD_BETA", CL_MEM_HOST_NO_ACCESS | CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, SIMD_BETA.second, SIMD_BETA.first),
        };
        resources[0].presentationName = "I/O buffer [0]";
        resources[1].presentationName = "I/O buffer [1]";
        resources[2].presentationName = "AES round T tables";

        resources[4].presentationName = "SIMD &alpha; table";
        resources[5].presentationName = "SIMD &beta; table";
        if(desc) return DescribeResources(*desc, resources, sizeof(resources) / sizeof(resources[0]), specials);
        auto errors(PrepareResources(resources, sizeof(resources) / sizeof(resources[0]), specials));
        if(errors.size()) return errors;

        typedef WorkGroupDimensionality WGD;
        KernelRequest kernels[] = {
            {
                "Luffa_1W.cl", "Luffa_1way", "-D LUFFA_HEAD",
                WGD(256),
                "$wuData, io0"
            },
            {
                "CubeHash_2W.cl", "CubeHash_2way", "",
                WGD(2, 32),
                "io0, io1"
            },
            {
                "SHAvite3_1W.cl", "SHAvite3_1way", "",
                WGD(64),
                "io1, io0, AES_T_TABLES, sh3_roundCount"
            },
            {
                "SIMD_16W.cl", "SIMD_16way", "",
                WGD(16, 4),
                "io0, io1, io0, SIMD_ALPHA, SIMD_BETA"
            },
            {
                "Echo_8W.cl", "Echo_8way", "-D AES_TABLE_ROW_1 -D AES_TABLE_ROW_2 -D AES_TABLE_ROW_3 -D ECHO_IS_LAST",
                WGD(8, 8),
                "io1, $candidates, $dispatchData, AES_T_TABLES"
            }
        };
        return PrepareKernels(kernels, sizeof(kernels) / sizeof(kernels[0]), specials, loadPathPrefix);
    }
    bool BigEndian() const { return true; }
    aulong GetDifficultyNumerator() const { return 0x0000000000FFFFFFull; }
};

}
