/*
 * Copyright (C) 2015 Massimo Del Zotto
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "AbstractAlgorithm.h"


void AbstractAlgorithm::DescribeResources(std::vector<ConfigDesc::MemDesc> &desc, const std::vector<ResourceRequest> &resources) {
    desc.reserve(resources.size());
    auto isHOST = [](cl_mem_flags mask) -> bool {
        bool ret = false;
        ret |= (mask & CL_MEM_USE_HOST_PTR) != 0;
        ret |= (mask & CL_MEM_ALLOC_HOST_PTR) != 0;
        ret |= (mask & CL_MEM_HOST_WRITE_ONLY) != 0; // not really necessary
        return ret != 0;
    };
    for(auto &res : resources) {
        if(res.immediate) continue; // whatever this holds true depends on implementation but usually irrelevant in terms of estimating consumption.
        ConfigDesc::MemDesc build;
        build.presentation = res.presentationName.empty()? res.name : res.presentationName;
        if(res.bytes != auint(res.bytes)) throw std::exception("Buffer exceeds 4GiB, not supported for the time being.");
        build.bytes = auint(res.bytes);
        build.memoryType = isHOST(res.memFlags)? ConfigDesc::as_host : ConfigDesc::as_device;
        // \sa DataDrivenAlgoFactory::ParseMemFlags
        if((res.memFlags & CL_MEM_HOST_NO_ACCESS) != 0) build.flags.push_back("gpu only");
        if((res.memFlags & CL_MEM_READ_ONLY) != 0) build.flags.push_back("ro");
        if((res.memFlags & CL_MEM_WRITE_ONLY) != 0) build.flags.push_back("wo");
        if((res.memFlags & CL_MEM_USE_HOST_PTR) != 0) build.flags.push_back("host memory");
        if((res.memFlags & CL_MEM_ALLOC_HOST_PTR) != 0) build.flags.push_back("host alloc");
        if((res.memFlags & CL_MEM_HOST_WRITE_ONLY) != 0) build.flags.push_back("host wo");
        if((res.memFlags & CL_MEM_HOST_READ_ONLY) != 0) build.flags.push_back("host ro");

        desc.push_back(std::move(build));
    }
}


void AbstractAlgorithm::RunAlgorithm(cl_command_queue q, asizei amount) {
    for(asizei loop = 0; loop < kernels.size(); loop++) {
        const auto &kern(kernels[loop]);
        for(auto param : kern.dtBindings) clSetKernelArg(kern.clk, param.first, sizeof(param.second.buff), &param.second.buff);
        // ^ \todo always rebind for the time being, interesting experiment is see how it performs

        asizei woff[3], wsize[3];
        memset(woff, 0, sizeof(woff));
        memset(wsize, 0, sizeof(wsize));
        /* Kernels used by M8M always have the same group format layout: given an N-dimensional kernel,
        - (N-1)th dimension is the hash being computed in global work -> number of hashes computed per workgroup in local work declaration
        - All previous dimensions are the "team" and can be easily pulled from declaration.
        Work offset leaves "team players" untouched while global work size is always <team size><total hashes>. */
        woff[kern.dimensionality - 1] = nonceBase;
        for(auto cp = 0u; cp < kern.dimensionality - 1; cp++) wsize[cp] = kern.wgs[cp];
        wsize[kern.dimensionality - 1] = amount;

        cl_int error = clEnqueueNDRangeKernel(q, kernels[loop].clk, kernels[loop].dimensionality, woff, wsize, kernels[loop].wgs, 0, NULL, NULL);
        if(error != CL_SUCCESS) {
            std::string ret("OpenCL error " + std::to_string(error) + " returned by clEnqueueNDRangeKernel(");
            auto identifier(Identify());
            ret += identifier.algorithm + '.' + identifier.implementation;
            ret += '[' + std::to_string(loop) + "])";
            throw ret;
        }
    }
    nonceBase += amount;
}
