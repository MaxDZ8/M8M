/*
 * Copyright (C) 2015 Massimo Del Zotto
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractAlgorithm.h"
#include "AbstractSpecialValuesProvider.h"
#include <set>

/*! The stop-n-wait dispatcher takes an algorithm and uses it to drive the GPU 1 unit of work at time.
It dispatches data and waits for result. It is basically the same thing M8M always did, which is very similar to legacy miners.
WRT legacy miners two differences come to mind.

Legacy miners dispatch all the work, force it to finish, then ask for a buffer map and wait for it again! This is due to their use
of out-of-order queues which is not really needed, especially as many algos are single step.
M8M dispatches all the work, including the map request and then **waits for it until finished**.
An initial version of Qubit also tried to dispatch one step at time but it was nonsensically overcomplicated for no benefit.
So in short I avoid a Finish (1) and a blocking read (2). Apparently this produces better interactivity. */
class StopWaitDispatcher : private AbstractSpecialValuesProvider {
public:
    AbstractAlgorithm &algo;

    StopWaitDispatcher(AbstractAlgorithm &drive) : algo(drive) {
        PrepareIOBuffers(algo.context, algo.hashCount);

        // Bind value names...
        SpecialValueBinding early;
        early.earlyBound = true;
        early.resource.buff = wuData;
        specials.push_back(NamedValue("$wuData", early));
        early.resource.buff = dispatchData;
        specials.push_back(NamedValue("$dispatchData", early));
        early.resource.buff = candidates;
        specials.push_back(NamedValue("$candidates", early));

        cl_int err = 0;
        queue = clCreateCommandQueue(algo.context, algo.device, 0, &err);
        if(!queue || err != CL_SUCCESS) throw "Could not create command queue for device!";
    }
    ~StopWaitDispatcher() {
        if(mapping) clReleaseEvent(mapping);
        if(nonces) clEnqueueUnmapMemObject(queue, candidates, nonces, 0, NULL, NULL);
        if(queue) clReleaseCommandQueue(queue);
    }


    void BlockHeader(const std::array<aubyte, 80> &header) { blockHeader = header; }
    void TargetBits(aulong reference) { targetBits = reference; }

    //! Tries to evolve algorithm state. The only thing that prevents an algorithm to evolve is completion of the mapping operations.
    //! \param [in,out] blockers contains a list of events representing completed operations. If the event I'm waiting for is in the set,
    //! I will remove it from the set of waiting events.
    AlgoEvent Tick(std::set<cl_event> &blockers) {
        // The first, most important thing to do is to free results so I can start again.
        if(mapping) {
            if(blockers.find(mapping) == blockers.cend()) return AlgoEvent::working;
            blockers.erase(mapping);
            return AlgoEvent::results;
        }
        if(algo.Overflowing()) return AlgoEvent::exhausted; // nothing to do

        cl_int err = 0;
        err = clEnqueueWriteBuffer(queue, wuData, CL_TRUE, 0, sizeof(blockHeader), blockHeader.data(), 0, NULL, NULL);
        if(err != CL_SUCCESS) throw std::string("CL error ") + std::to_string(err) + " while attempting to update $wuData";

        cl_uint buffer[5]; // taken as is from M8M FillDispatchData... how ugly!
        buffer[0] = 0;
        buffer[1] = static_cast<cl_uint>(targetBits >> 32);
        buffer[2] = static_cast<cl_uint>(targetBits);
        buffer[3] = 0;
        buffer[4] = 0;
        err = clEnqueueWriteBuffer(queue, dispatchData, CL_TRUE, 0, sizeof(buffer), buffer, 0, NULL, NULL);
        if(err != CL_SUCCESS) throw std::string("CL error ") + std::to_string(err) + " while attempting to update $dispatchData";

        cl_uint zero = 0;
        clEnqueueWriteBuffer(queue, candidates, true, 0, sizeof(cl_uint), &zero, 0, NULL, NULL);

        algo.RunAlgorithm(queue, algo.hashCount);
        dispatchedHeader = blockHeader;

        nonces = reinterpret_cast<cl_uint*>(clEnqueueMapBuffer(queue, candidates, CL_FALSE, CL_MAP_READ, 0, nonceBufferSize, 0, NULL, &mapping, &err));
        if(err != CL_SUCCESS) throw std::string("CL error ") + std::to_string(err) + " attempting to map nonce buffers.";

        return AlgoEvent::dispatched; // this could be ae_working as well but returning ae_dispatched at least once sounds good.
    }


    void GetEvents(std::vector<cl_event> &events) const {
        if(mapping) events.push_back(mapping);
    }


    MinedNonces GetResults() {
        const asizei count = *nonces;
        MinedNonces ret(dispatchedHeader);
        ret.hashes.reserve(count * algo.uintsPerHash);
        ret.nonces.reserve(count);
        auto incremental(nonces);
        incremental++;
        for(asizei cp = 0; cp < count; cp++) {
            ret.nonces.push_back(*incremental);
            incremental++;
            for(asizei h = 0; h < algo.uintsPerHash; h++) ret.hashes.push_back(incremental[h]);
            incremental += algo.uintsPerHash;
        }
        clEnqueueUnmapMemObject(queue, candidates, nonces, 0, NULL, NULL);
        nonces = nullptr;
        clReleaseEvent(mapping);
        mapping = 0;
        return ret;
    }


    void Push(LateBinding &slot, asizei valueIndex) {
        // Do nothing. Stop-n-wait has only early bound buffers.
    }

    //! So I have more private stuff.
    AbstractSpecialValuesProvider& AsValueProvider() { return *this; }

    //! This is needed mainly for testing. No real need to have it there but more private stuff.
    cl_command_queue GetQueue() const { return queue; }

    //! Returns true if the header **might** be returned by a future call to GetResults
    bool IsInFlight(const std::array<aubyte, 80> &test) {
        return test == dispatchedHeader || test == blockHeader;
    }

private:
    cl_mem wuData = 0, dispatchData = 0;
    cl_mem candidates = 0;
    asizei nonceBufferSize = 0;
    cl_event mapping = 0;
    cl_command_queue queue = 0;
    auint *nonces = nullptr;
    std::array<aubyte, 80> dispatchedHeader; //!< block dispatched to last RunAlgorithm
    std::array<aubyte, 80> blockHeader; //!< block to dispatch at NEXT RunAlgorithm!
    aulong targetBits;

    void PrepareIOBuffers(cl_context context, asizei hashCount){
        cl_int error;
        asizei byteCount = 80;
        wuData = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY, byteCount, NULL, &error);
        if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create wuData buffer.";
        byteCount = 5 * sizeof(cl_uint);
        dispatchData = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY, byteCount, NULL, &error);
        if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create dispatchData buffer.";
        // The candidate buffer should really be dependant on difficulty setting but I take it easy.
        byteCount = hashCount / (16 * 1024);
        //! \todo pull the whole hash down so I can check mismatches
        if(byteCount < 32) byteCount = 32;
        byteCount *= sizeof(cl_uint) * (1 + algo.uintsPerHash);
        byteCount += 4; // initial candidate count
        nonceBufferSize = byteCount;
        candidates = clCreateBuffer(context, CL_MEM_ALLOC_HOST_PTR, byteCount, NULL, &error);
        if(error) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to resulting nonces buffer.";
    }
};
