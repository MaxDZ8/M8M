/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <CL/cl.h>
#include <array>

/*! OpenCL has a C++ wrapper but it's quite old in terms of revisions (4 years) so I roll my own.
Hopefully this will make it self-documenting.
Note that my wrapper has RAII and exceptions in many paths. */
struct OpenCL12Wrapper {
    struct Device {
        cl_device_id clid;
	};
    struct Platform {
        cl_platform_id clid;
        std::vector<Device> devices;
	};
    std::vector<Platform> platforms;

    OpenCL12Wrapper() {
        const asizei count = 8;
        cl_platform_id store[count];
        cl_uint avail;
        cl_int err = clGetPlatformIDs(count, store, &avail);
        const asizei numPlatforms(avail);
        if(err != CL_SUCCESS) throw std::exception("Failed to build OpenCL platforms list.");
        if(avail == 0) throw std::exception("Apparently there's not a single OpenCL platform on this system.");
        platforms.reserve(avail);
        asizei total = 8;
        std::unique_ptr<cl_device_id[]> enumerate(new cl_device_id[total]);
        for(asizei loop = 0; loop < numPlatforms; loop++) {
            err = clGetDeviceIDs(store[loop], CL_DEVICE_TYPE_GPU, total, enumerate.get(), &avail);
            if(err != CL_SUCCESS) throw std::exception("Could not enumerate devices.");
            if(avail > total) {
                total *= 2;
                enumerate.reset(new cl_device_id[total]);
                loop--;
                continue;
			}
            platforms.push_back(Platform());
            platforms.back().clid = store[loop];
            platforms.back().devices.reserve(avail);
            for(asizei cp = 0; cp < avail; cp++) {
                platforms.back().devices.push_back(Device());
                platforms.back().devices.back().clid = enumerate[cp];
			}
		}
	}
	cl_device_id GetLessUsedDevice() const {
		return platforms[0].devices[0].clid;
	}
	cl_platform_id GetPlatform(cl_device_id match) {
		auto el(std::find_if(platforms.cbegin(), platforms.cend(), [match](const Platform &test) {
			for(asizei loop = 0; loop < test.devices.size(); loop++) {
				if(test.devices[loop].clid == match) return true;
			}
			return false;
		}));
		if(el == platforms.cend()) return nullptr;
		return el->clid;
	}

	std::pair<auint, auint> ExtractCLVersion(char *string) {
		// guaranteed to be "OpenCL " <mag ver> '.' <min ver> ' ' ...
		string += strlen("OpenCL ");
		aulong minor = strtoul(string, &string, 10);
		string++;
		aulong major = strtoul(string, NULL, 10);
		return std::make_pair(auint(minor), auint(major));
	}
	
	typedef void (CL_CALLBACK *ErrorFunc)(const char *err, const void *priv, size_t privSz, void *userData);
	typedef cl_event WaitEvent;
};
