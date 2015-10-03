/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractAlgorithm.h"

class DataDrivenAlgorithm : public AbstractAlgorithm {
public:
    DataDrivenAlgorithm(asizei numHashes, cl_context ctx, cl_device_id dev, asizei candHashUints) :
        AbstractAlgorithm(numHashes, ctx, dev, candHashUints) {
    }
    SignedAlgoIdentifier identifier;
    SignedAlgoIdentifier Identify() const { return identifier; }


    /*! Performs all the heavy duty required to create the resources to run the algorithm. Returns a list of all errors encountered.
    Those are really errors, so if something non-empty is returned you should bail out.
    Call this immediately after CTOR. Must be called before Tick, GetEvents, GetResults.
    \note The easiest way to implement this is to call DescribeResources and return before PrepareResources. */
    std::vector<std::string> Init(AbstractSpecialValuesProvider &specials, SourceCodeBufferGetterFunc loader, const std::vector<ResourceRequest> &res, const std::vector<KernelRequest> &kern) {
        auto errors(PrepareResources(res, specials));
        if(errors.size()) return errors;
        return PrepareKernels(kern, specials, loader);
    }

private:
    /*! Derived classes are expected to call this somewhere in their ctor. It deals with allocating memory and eventually initializing it in a
    data-driven way. Note special resources cannot be created using this, at least in theory. Just create them in the ctor before PrepareKernels.
    While this is allowed to throw, it is suggested to produce a list of errors to be returned by Init(). */
    std::vector<std::string> PrepareResources(const std::vector<ResourceRequest> &resources, const AbstractSpecialValuesProvider &prov) {
        std::vector<std::string> errors;
        for(const auto &res : resources) {
            if(resHandles.find(res.name) != resHandles.cend()) throw std::string("Duplicated resource name \"" + res.name + '"');
            if(prov.SpecialValue(res.name)) {
                errors.push_back("Trying to allocate special resource \"" + res.name + "\" from algorithm, invalid operation.");
                continue;
            }
            resRequests.push_back(res);
            if(res.immediate) continue; // nothing to allocate here
            ScopedFuncCall popLast([this]() { resRequests.pop_back(); });
            cl_mem build = 0;
            ScopedFuncCall relMem([&build]() { if(build) clReleaseMemObject(build); });
            cl_int err = 0;
            asizei count = errors.size();
            if(res.imageDesc.image_width) {
                aubyte *src = const_cast<aubyte*>(res.initialData);
                build = clCreateImage(context, res.memFlags, &res.channels, &res.imageDesc, src, &err);
                if(err == CL_INVALID_VALUE) errors.push_back("Invalid flags specified for \"" + res.name + '"');
                else if(err == CL_INVALID_IMAGE_FORMAT_DESCRIPTOR)  errors.push_back("Invalid image format descriptor for \"" + res.name + '"');
                else if(err == CL_INVALID_IMAGE_DESCRIPTOR) errors.push_back("Invalid image descriptor for \"" + res.name + '"');
                else if(err == CL_INVALID_IMAGE_SIZE) errors.push_back("Image \"" + res.name + "\" is too big!");
                else if(err == CL_INVALID_HOST_PTR) errors.push_back("Invalid host data for \"" + res.name + '"');
                else if(err == CL_IMAGE_FORMAT_NOT_SUPPORTED) errors.push_back("Invalid image format for \"" + res.name + '"');
                else if(err != CL_SUCCESS) errors.push_back("Some error while creating \"" + res.name + "\")");
                if(errors.size() != count) continue;
            }
            else {
                cl_uint extraFlags = 0;
                if(res.initialData) {
                    if(res.useProvidedBuffer) extraFlags |= CL_MEM_USE_HOST_PTR;
                    else extraFlags |= CL_MEM_COPY_HOST_PTR;
                }
                build = clCreateBuffer(context, res.memFlags | extraFlags, res.bytes, const_cast<aubyte*>(res.initialData), &err);
                if(err == CL_INVALID_VALUE) errors.push_back("Invalid flags specified for \"" + res.name + '"');
                else if(err == CL_INVALID_BUFFER_SIZE) errors.push_back("Invalid buffer size for \"" + res.name + "\": " + std::to_string(res.bytes));
                else if(err == CL_INVALID_HOST_PTR) errors.push_back("Invalid host data for \"" + res.name + '"');
                else if(err != CL_SUCCESS) errors.push_back("Some error while creating \"" + res.name + '"');
                if(errors.size() != count) continue;
            }
            resHandles.insert(std::make_pair(res.name, build));
            relMem.Dont();
            popLast.Dont();
        }
        return errors;
    }

    //! Similarly, kernels are described by data and built by resolving the previously declared resources. Device used to pull out eventual error logs.
    std::vector<std::string> PrepareKernels(const std::vector<KernelRequest> &kernels, AbstractSpecialValuesProvider &special, SourceCodeBufferGetterFunc loader) {
        // By delegating the getter func to resolve source code buffers, this gets way simplier.
        std::vector<std::pair<const char*, asizei>> sources;
        std::vector<std::string> errors;
        for(const auto &k : kernels) {
            std::vector<std::string> fileErrors;
            sources.push_back(loader(fileErrors, k.fileName));
            if(fileErrors.size()) {
                for(const auto &el : fileErrors) errors.push_back(std::move(k.fileName + ": " + el));
            }
        }
        if(errors.size()) return errors;
        // Run all the compile calls. One program must be built for each requested kernel as it will go with different compile options but they have the same source.
        // OpenCL is reference counted (bleargh) so programs can go at the end of this function.
        // I wanted to do this asyncronously but BuildProgram goes with notification functions instead of events (?) so I would have to do that multithreaded.
        // Or, I might just Sleep one second. Not bad either but I cannot be bothered in getting a sleep call here.
        // By the way, CL spec reads as error: "CL_INVALID_OPERATION if the build of a program executable for any of the devices listed in device_list by a previous
        // call to clBuildProgram for program has not completed." So this is really non concurrent?
        std::vector<cl_program> progs(kernels.size());
        ScopedFuncCall clearProgs([&progs]() { for(auto el : progs) { if(el) clReleaseProgram(el); } });
        for(asizei loop = 0; loop < kernels.size(); loop++) {
            const char *str = sources[loop].first;
            const asizei len = sources[loop].second;
            cl_int err = 0;
            cl_program created = clCreateProgramWithSource(context, 1, &str, &len, &err);
            if(err != CL_SUCCESS) {
                errors.push_back(std::string("Failed to create program \"") + kernels[loop].fileName + '"');
                continue;
            }
            progs[loop] = created;

            err = clBuildProgram(created, NULL, 0, kernels[loop].compileFlags.c_str(), NULL, NULL);
            std::string errString;
            if(err == CL_INVALID_BUILD_OPTIONS) {
                errString = std::string("Invalid compile options \"");
                errString += kernels[loop].compileFlags + "\" for ";
                errString += kernels[loop].fileName + '.' + kernels[loop].entryPoint;
            }
            else if(err != CL_SUCCESS) {
                errString = std::string("OpenCL error ") + std::to_string(err) + " for ";
                errString += kernels[loop].fileName + '.' + kernels[loop].entryPoint + ", attempted compile with \"";
                errString += kernels[loop].compileFlags + '"';
            }
            if(errString.length()) {
                std::vector<char> log;
                asizei requiredChars;
                err = clGetProgramBuildInfo(created, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &requiredChars);
                if(err != CL_SUCCESS) {
                    errors.push_back(errString + " (also failed to call clGetProgramBuildInfo successfully)"); // unrecognized compile options meh
                    continue;
                }
                else {
                    log.resize(requiredChars);
                    err = clGetProgramBuildInfo(created, device, CL_PROGRAM_BUILD_LOG, log.size(), log.data(), &requiredChars);
                    if(err != CL_SUCCESS) errString + "(also failed to get build error log)";
                    errors.push_back(errString + '\n' + "ERROR LOG:\n" + std::string(log.data(), requiredChars));
                }
            }
        }
        if(errors.size()) return errors;
        this->kernels.reserve(kernels.size());

        for(asizei loop = 0; loop < kernels.size(); loop++) {
            cl_int err;
            cl_kernel kern = clCreateKernel(progs[loop], kernels[loop].entryPoint.c_str(), &err);
            if(err != CL_SUCCESS) {
                errors.push_back(std::string("Could not create kernel \"") + kernels[loop].fileName + ':' + kernels[loop].entryPoint + "\", error " + std::to_string(err));
                continue;
            }
            this->kernels.push_back(KernelDriver(kernels[loop].groupSize, kern));
        }
        if(errors.size()) return errors;
        for(asizei loop = 0; loop < kernels.size(); loop++) BindParameters(this->kernels[loop], kernels[loop], special, loop);
        return errors;
    }


    //! Called at the end of PrepareKernels. Given a cl_kernel and its originating KernelRequest object, generates a stream of clSetKernelArg according
    //! to its internal bindings, resHandles and resRequests (for immediates).
    void BindParameters(KernelDriver &kdesc, const KernelRequest &bindings, AbstractSpecialValuesProvider &disp, asizei kernelIndex) {
        // First split out the bindings.
        std::vector<std::string> params;
        asizei comma = 0, prev = 0;
        while((comma = bindings.params.find(',', comma)) != std::string::npos) {
            params.push_back(std::string(bindings.params.cbegin() + prev, bindings.params.cbegin() + comma));
            comma++;
            prev = comma;
        }
        params.push_back(std::string(bindings.params.cbegin() + prev, bindings.params.cend()));
        for(auto &name : params) {
            const char *begin = name.c_str();
            const char *end = name.c_str() + name.length() - 1;
            while(begin < end && *begin == ' ') begin++;
            while(end > begin && *end == ' ') end--;
            end++;
            if(begin != name.c_str() || end != name.c_str() + name.length()) name.assign(begin, end - begin);
            if(name.length() == 0) throw "Kernel binding has empty name.";
        }
        // Now look em up, some are special and perhaps they might need an unified way of mangling (?)
        // The main problem here is that I need to produce persistent buffers for Push'ing so late bounds first!
        asizei lateBound = 0;
        for(cl_uint loop = 0; loop < params.size(); loop++) {
            const auto &name(params[loop]);
            SpecialValueBinding desc;
            if(disp.SpecialValue(desc, name)) {
                if(desc.earlyBound == false) lateBound++;
            }
        }
        kdesc.dtBindings.resize(lateBound); // .reserve also good
        lateBound = 0;
        auto paramError = [this, kernelIndex](cl_uint paramIndex, const std::string &name, cl_int err) -> std::string {
            std::string ret("OpenCL error " + std::to_string(err) + " returned by clSetKernelArg(), attepting to statically bind ");
            auto identifier(Identify());
            ret += identifier.algorithm + '.' + identifier.implementation;
            ret += '[' + std::to_string(kernelIndex) + "], parameter [" + std::to_string(paramIndex) + ", " + name + ']';
            throw ret;
        };
        for(cl_uint loop = 0; loop < params.size(); loop++) {
            const auto &name(params[loop]);
            SpecialValueBinding desc;
            if(disp.SpecialValue(desc, name)) {
                if(desc.earlyBound) clSetKernelArg(kdesc.clk, loop, sizeof(desc.resource.buff), &desc.resource.buff);
                else {
                    kdesc.dtBindings[lateBound].first = loop;
                    disp.Push(kdesc.dtBindings[lateBound].second, desc.resource.index);
                    lateBound++;
                }
                continue;
            }
            auto bound = resHandles.find(name);
            if(bound != resHandles.cend()) {
                cl_int err = clSetKernelArg(kdesc.clk, loop, sizeof(cl_mem), &bound->second);
                if(err != CL_SUCCESS) throw paramError(loop, name, err);
                continue;
            }
            // immediate, maybe
            auto imm = std::find_if(resRequests.cbegin(), resRequests.cend(), [&name](const ResourceRequest &rr) {
                return rr.immediate && rr.name == name;
            });
            if(imm == resRequests.cend()) throw std::string("Could not find parameter \"") + name + '"';
            cl_int err = clSetKernelArg(kdesc.clk, loop, imm->bytes, imm->initialData);
            if(err != CL_SUCCESS) throw paramError(loop, name, err);
        }
    }
};
