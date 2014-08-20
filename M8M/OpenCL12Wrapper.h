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
        cl_device_id clid; //!< \todo are those unique across different platforms? I'd have to re-read.
		asizei linearIndex;
		struct Type {
			bool cpu, gpu, accelerator, /*custom,*/ defaultDevice;
		} type;
	};
    struct Platform {
        cl_platform_id clid;
        std::vector<Device> devices;
		Platform() : clid(0) { }
		bool IsYours(const Device &dev) const {
			return devices.cend() != std::find_if(devices.cbegin(), devices.cend(), [&dev](const Device &test) { return test.clid == dev.clid; });
		}
	};
    typedef std::vector<Platform> ComputeNodes;
    ComputeNodes platforms;

    OpenCL12Wrapper() {
        const asizei count = 8;
        cl_platform_id store[count];
        cl_uint avail = count;
        cl_int err = clGetPlatformIDs(count, store, &avail);
        const asizei numPlatforms(avail);
        if(err != CL_SUCCESS) throw std::exception("Failed to build OpenCL platforms list.");
		platforms.resize(numPlatforms);
		std::vector<cl_device_id> devBuff(8);
		asizei linearIndex = 0;
		for(asizei loop = 0; loop < numPlatforms; loop++) {
			platforms[loop].clid = store[loop];
			asizei tried = 0;
			while(tried < 5) {
				tried++;
				err = clGetDeviceIDs(store[loop], CL_DEVICE_TYPE_ALL, devBuff.size(), devBuff.data(), &avail);
				if(avail > devBuff.size()) {
					devBuff.resize(avail);
					continue;
				}
				/* custom devices? */
				for(asizei dev = 0; dev < avail; dev++) {
					Device::Type type;
					cl_device_type clType;
					err = clGetDeviceInfo(devBuff[dev], CL_DEVICE_TYPE, sizeof(clType), &clType, NULL);
					if(err != CL_SUCCESS) throw std::exception("This was supposed to never happen, system must be in bad state!");
					type.cpu = (clType & CL_DEVICE_TYPE_CPU) != 0;
					type.gpu = (clType & CL_DEVICE_TYPE_GPU) != 0;
					type.accelerator = (clType & CL_DEVICE_TYPE_ACCELERATOR) != 0;
					type.defaultDevice = (clType & CL_DEVICE_TYPE_DEFAULT) != 0;
					platforms[loop].devices.push_back(Device());
					platforms[loop].devices.back().clid = devBuff[dev];
					platforms[loop].devices.back().type = type;
					platforms[loop].devices.back().linearIndex = linearIndex++;
				}
				break;
			}
			if(tried == 5) throw std::exception("Something fishy is going on with device enumeration!");
		}
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

	enum DeviceGroupInfoString {
		dgis_profile,
		dgis_version,
		dgis_name,
		dgis_vendor,
		dgis_extensions
	};

	//! OpenCL calls them "Platforms", what are they? Groups of compatible devices which go along nice together.
	//! Or maybe are the different drivers installed. Defined so I could template this on need.
	//! Not considered a performance path.
	static std::string GetDeviceGroupInfo(const Platform &plat, DeviceGroupInfoString prop) {
		std::vector<char> text;
		asizei avail = 64;
		text.resize(avail);
		cl_platform_info clProp;
		switch(prop) {
		case dgis_profile: clProp = CL_PLATFORM_PROFILE; break;
		case dgis_version: clProp = CL_PLATFORM_VERSION; break;
		case dgis_name: clProp = CL_PLATFORM_NAME; break;
		case dgis_vendor: clProp = CL_PLATFORM_VENDOR; break;
		case dgis_extensions: clProp = CL_PLATFORM_EXTENSIONS; break;
		default: throw std::exception("Impossible. Code out of sync?");
		}
		cl_int err = clGetPlatformInfo(plat.clid, clProp, avail, text.data(), &avail);
		if(err == CL_INVALID_VALUE) {
			text.resize(avail);
			err = clGetPlatformInfo(plat.clid, clProp, avail, text.data(), &avail);
			if(err != CL_SUCCESS) throw std::exception("Something went very wrong with CL platform properties.");
		}
		return std::string(text.data(), text.data() + avail - 1);
	}
	static const char* GetName() { return "OpenCL 1.2"; }

	enum DeviceInfoString {
		dis_chip,
		dis_vendor,
		dis_driverVersion,
		dis_profile,
		dis_apiVersion,
		dis_extensions
	};
	enum DeviceInfoUint {
		diu_vendorID,
		diu_clusters,
		diu_coreClock
	};
	enum DeviceInfoUnsignedLong {
		diul_maxMemAlloc,
		diul_globalMemBytes,
		diul_ldsBytes,
		diul_globalMemCacheBytes,
		diul_cbufferBytes,
	};
	enum DeviceInfoBool {
		dib_ecc,
		dib_huma,
		dib_littleEndian,
		dib_available,
		dib_compiler,
		dib_linker
	};
	static std::string GetDeviceInfo(const Device &dev, DeviceInfoString prop) {
		asizei avail;
		cl_device_info clProp = Native(prop);
		cl_int err = clGetDeviceInfo(dev.clid, clProp, 0, NULL, &avail);
		std::vector<char> text(avail);
		err = clGetDeviceInfo(dev.clid, clProp, avail, text.data(), &avail);
		if(err != CL_SUCCESS) throw std::exception("Something went very wrong with CL platform properties.");
		return std::string(text.data(), text.data() + avail - 1);
	}
	static auint GetDeviceInfo(const Device &dev, DeviceInfoUint prop) {
		cl_uint value;
		cl_device_info clProp = Native(prop);
		cl_int err = clGetDeviceInfo(dev.clid, clProp, sizeof(value), &value, NULL);
		if(err != CL_SUCCESS) throw std::exception("Something went wrong while probing device info.");
		return value;
	}
	static aulong GetDeviceInfo(const Device &dev, DeviceInfoUnsignedLong prop) {
		cl_long value;
		cl_device_info clProp = Native(prop);
		cl_int err = clGetDeviceInfo(dev.clid, clProp, sizeof(value), &value, NULL);
		if(err != CL_SUCCESS) throw std::exception("Something went wrong while probing device info.");
		return value;
	}
	static bool GetDeviceInfo(const Device &dev, DeviceInfoBool prop) {
		cl_bool value;
		cl_device_info clProp = Native(prop);
		cl_int err = clGetDeviceInfo(dev.clid, clProp, sizeof(value), &value, NULL);
		if(err != CL_SUCCESS) throw std::exception("Something went wrong while probing device info.");
		return value != 0;
	}

	enum LDSType {
		ldsType_none,
		ldsType_global,
		ldsType_dedicated
	};
	LDSType GetDeviceLDSType(const Device &dev) {
		cl_device_local_mem_type value;
		cl_int err = clGetDeviceInfo(dev.clid, CL_DEVICE_LOCAL_MEM_TYPE, sizeof(value), &value, NULL);
		if(err != CL_SUCCESS) throw std::exception("Something went wrong while probing device info.");
		if(value == CL_LOCAL) return ldsType_dedicated;
		return value == CL_GLOBAL? ldsType_global : ldsType_none;
	}

	Device* GetDeviceLinear(asizei index) const {
		for(asizei p = 0; p < platforms.size(); p++) {
			if(index < platforms[p].devices.size()) return const_cast<Device*>(&platforms[p].devices[index]);
			index -= platforms[p].devices.size();
		}
		return nullptr;
	}

	Platform* GetPlatform(const Device &dev) const { // could really return a reference since I enumerate everything but... what about detachable devices?
		for(asizei p = 0; p < platforms.size(); p++) {
			if(platforms[p].IsYours(dev)) return const_cast<Platform*>(&platforms[p]);
		}
		return nullptr;
	}

private:
	static cl_device_info Native(DeviceInfoString e) {
		switch(e) {
		case dis_chip: return CL_DEVICE_NAME;
		case dis_vendor: return CL_DEVICE_VENDOR;
		case dis_driverVersion: return CL_DRIVER_VERSION;
		case dis_profile: return CL_DEVICE_PROFILE;
		case dis_apiVersion: return CL_DEVICE_VERSION;
		case dis_extensions: return CL_DEVICE_EXTENSIONS;
		}
		throw std::exception("Missing an enum...");
	}
	static cl_device_info Native(DeviceInfoUint e) {
		switch(e) {
		case diu_vendorID: return CL_DEVICE_VENDOR_ID;
		case diu_clusters: return CL_DEVICE_MAX_COMPUTE_UNITS;
		case diu_coreClock: return CL_DEVICE_MAX_CLOCK_FREQUENCY;
		}
		throw std::exception("Missing an enum...");
	}
	static cl_device_info Native(DeviceInfoUnsignedLong e) {
		switch(e) {
		case diul_maxMemAlloc: return CL_DEVICE_MAX_MEM_ALLOC_SIZE;
		case diul_globalMemBytes: return CL_DEVICE_GLOBAL_MEM_SIZE;
		case diul_ldsBytes: return CL_DEVICE_LOCAL_MEM_SIZE;
		case diul_globalMemCacheBytes: return CL_DEVICE_GLOBAL_MEM_CACHE_SIZE;
		case diul_cbufferBytes: return CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE;
		}
		throw std::exception("Missing an enum...");
	}
	static cl_device_info Native(DeviceInfoBool e) {
		switch(e) {
		case dib_ecc: return CL_DEVICE_ERROR_CORRECTION_SUPPORT;
		case dib_huma: return CL_DEVICE_HOST_UNIFIED_MEMORY;
		case dib_littleEndian: return CL_DEVICE_ENDIAN_LITTLE;
		case dib_available: return CL_DEVICE_AVAILABLE;
		case dib_compiler: return CL_DEVICE_COMPILER_AVAILABLE;
		case dib_linker: return CL_DEVICE_LINKER_AVAILABLE;
		}
		throw std::exception("Missing an enum...");
	}
};
