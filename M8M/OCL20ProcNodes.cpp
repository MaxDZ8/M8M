/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "OCL20ProcNodes.h"


#include <iostream>
using std::cout;
using std::endl;

const asizei OCL20ProcNodes::MAX_PLATFORMS = 8;
const asizei OCL20ProcNodes::MAX_GPU_COUNT = 8;


#define COPY(what) what = other.what;


OCL20ProcNodes::DeviceHWInfo::DeviceHWInfo(const DeviceHWInfo &other) {
	COPY(id);
	COPY(computeUnits);
	COPY(workItemsPerGroupMax);
	COPY(biggestMemAllocBytes);
	COPY(globalMemBytes);
	COPY(profilerResolution);
	COPY(littleEndian);
	COPY(canCompile);
	COPY(canLink);
	COPY(name);
	COPY(workItemSize);
	COPY(localMemBytes);
}


OCL20ProcNodes::ImageCaps::ImageCaps(const ImageCaps &other) {
	COPY(supported);
	COPY(readMax);
	COPY(writeMax);
	COPY(rowPitch);
	COPY(twoMaxW);
	COPY(twoMaxH);
	COPY(buffOneDImagePixelsMax);
	COPY(arraySlicesMax);
}


#undef COPY


OCL20ProcNodes::DeviceHWInfo::DeviceHWInfo(cl_device_id &dev) {
	Probe(dev, CL_DEVICE_VENDOR_ID, id);
	Probe(dev, CL_DEVICE_MAX_COMPUTE_UNITS, computeUnits);

	cl_uint widim;
	Probe(dev, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, widim);
	workItemSize.resize(widim);
	clGetDeviceInfo(dev, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(workItemSize[0]) * workItemSize.size(), workItemSize.data(), NULL);

	Probe(dev, CL_DEVICE_MAX_WORK_GROUP_SIZE, workItemsPerGroupMax);
	Probe(dev, CL_DEVICE_MAX_MEM_ALLOC_SIZE, biggestMemAllocBytes);
	Probe(dev, CL_DEVICE_GLOBAL_MEM_SIZE, globalMemBytes);
	Probe(dev, CL_DEVICE_LOCAL_MEM_SIZE, localMemBytes);
	Probe(dev, CL_DEVICE_PROFILING_TIMER_RESOLUTION, profilerResolution);
	Probe(dev, CL_DEVICE_ENDIAN_LITTLE, littleEndian);
	Probe(dev, CL_DEVICE_COMPILER_AVAILABLE, canCompile);
	Probe(dev, CL_DEVICE_LINKER_AVAILABLE, canLink);
	asizei stringLen = 128;
	std::unique_ptr<char[]> temp(new char[stringLen]);
	while(name.empty()) {
		asizei count;
		auto ret = clGetDeviceInfo(dev, CL_DEVICE_NAME, stringLen, temp.get(), &count);
		if(ret == CL_SUCCESS) name = std::string(temp.get());
		else if(ret == CL_INVALID_VALUE) {
			stringLen += 128;
			temp.reset(new char[stringLen]);
		}
	}
}



OCL20ProcNodes::ImageCaps::ImageCaps(cl_device_id &dev) {
	Probe(dev, CL_DEVICE_IMAGE_SUPPORT, supported);
	if(supported) {
		Probe(dev, CL_DEVICE_MAX_READ_IMAGE_ARGS, readMax);
		Probe(dev, CL_DEVICE_MAX_WRITE_IMAGE_ARGS, writeMax);
		Probe(dev, CL_DEVICE_IMAGE_PITCH_ALIGNMENT, rowPitch);
		Probe(dev, CL_DEVICE_IMAGE2D_MAX_WIDTH, twoMaxW);
		Probe(dev, CL_DEVICE_IMAGE2D_MAX_HEIGHT, twoMaxH);
		Probe(dev, CL_DEVICE_IMAGE_MAX_BUFFER_SIZE, buffOneDImagePixelsMax);
		Probe(dev, CL_DEVICE_IMAGE_MAX_ARRAY_SIZE, arraySlicesMax);
	}
}


asizei OCL20ProcNodes::Enumerate() {
	cl_uint count = 0;
	cl_platform_id pid[MAX_PLATFORMS];
	if(CL_SUCCESS != clGetPlatformIDs(MAX_PLATFORMS, pid, &count)) throw std::exception("OpenCL implemntation looks terribly broken, could not even init.");
	if(count == 0) throw std::exception("No CL platforms available");
	const cl_platform_info paramName[] = { CL_PLATFORM_PROFILE, CL_PLATFORM_VERSION, CL_PLATFORM_NAME, CL_PLATFORM_VENDOR, CL_PLATFORM_EXTENSIONS };
	std::string paramString[sizeof(paramName) / sizeof(paramName[0])];
	asizei blobSize = 1024;
	std::unique_ptr<char[]> blob(new char[blobSize]);
	for(asizei loop = 0; loop < count; loop++) {
		asizei charCount = 0;
		//cout<<"Platform "<<loop<<endl;
		for(asizei inner = 0; inner < sizeof(paramName) / sizeof(paramName[0]); inner++) {
			auto ret = clGetPlatformInfo(pid[loop], paramName[inner], blobSize, blob.get(), &charCount);
			if(ret == CL_INVALID_VALUE) {
				blobSize += 1024;
				blob.reset(new char[blobSize]);
				inner--;
				continue;
			}
			if(ret != CL_SUCCESS) throw std::exception("Some error while enumerating platform properties.");
			//cout<<blob.get()<<endl;
			paramString[inner] = std::string(blob.get());
		}
		platforms.push_back(Platform(pid[loop], paramString[3], paramString[2], paramString[1], paramString[0], paramString[4]));
	}
	if(platforms.empty()) throw std::exception("No CL platforms found.");

	cl_device_id devices[MAX_GPU_COUNT];
	for(asizei loop = 0; loop < platforms.size(); loop++) {
		asizei deviceCount = 0;
		cl_int ret = clGetDeviceIDs(platforms[loop].id, CL_DEVICE_TYPE_GPU, MAX_GPU_COUNT, devices, &deviceCount);
		if(ret == CL_DEVICE_NOT_FOUND) {
			platforms.erase(platforms.begin() + loop);
			loop--;
			continue;
		}
		if(ret != CL_SUCCESS)  throw std::exception("Some error while enumerating devices.");
		platforms[loop].devices.reserve(deviceCount);

		for(asizei inner = 0; inner < deviceCount; inner++) {
			Device test(devices[inner]);
			if(test.img.supported) {
				platforms[loop].devices.push_back(Device(devices[inner]));
			}
		}
	}
	count = 0;
	for(asizei loop = 0; loop < platforms.size(); loop++) count += platforms[loop].devices.size();
	return count;
}


asizei OCL20ProcNodes::GetAdapterCount() const {
	asizei count = 0;
	for(asizei loop = 0; loop < platforms.size(); loop++) count += platforms[loop].devices.size();
	return count;
}


std::wstring OCL20ProcNodes::GetInfo(asizei adapter, AdapterProperty what) const {
	const asizei initial = adapter;
	asizei pindex = 0;
	for(asizei loop = 0; loop < platforms.size(); loop++) {
		if(adapter < platforms[loop].devices.size()) break;
		adapter -= platforms[loop].devices.size();
		pindex++;
	}
	if(pindex >= platforms.size()) throw std::exception("No such device.");
	const Platform &p(platforms[pindex]);
	const Device &device(p.devices[adapter]);
	auto makeHexWstring = [](auint value) -> std::wstring {
		std::wstringstream out;
		out<<"0x"<<std::setfill(L'0');
		out<<std::hex<<value;
		return out.str();
	};
	auto makeWstring = [](const std::string &str) -> std::wstring {
		std::unique_ptr<wchar_t[]> copy(new wchar_t[str.length() + 1]);
		for(asizei cp = 0; cp < str.length(); cp++) copy.get()[cp] = str[cp];
		copy.get()[str.length()] = 0;
		return std::wstring(copy.get());
	};
	auto yesno = [](const bool flag) { return flag? "true" : "false"; };
	switch(what) {
	case ap_name: return makeWstring(device.hw.name);
	case ap_vendor: return makeWstring(p.vendor);
	case ap_device: return makeHexWstring(device.hw.id);
	case ap_processor: return makeWstring(device.hw.name);
	case ap_revision: return makeWstring(p.version);
	case ap_misc: {
		std::wstringstream out;
		out<<"CL_ext="<<makeWstring(p.extensions)<<endl
			<<"Platform name="<<makeWstring(p.name)<<endl
			<<"Profile="<<makeWstring(p.profile)<<endl
			<<"Platform index="<<makeHexWstring(pindex)<<endl
			<<"Concurrent processing units="<<device.hw.computeUnits<<endl
			<<"Processing unit parallelism="<<device.hw.workItemsPerGroupMax<<endl
			<<"Little endian="<<yesno(device.hw.littleEndian != 0)<<endl
			<<"Max bytes per alloc="<<device.hw.biggestMemAllocBytes<<endl
			<<"Work size max dimensions=(";
		for(asizei loop = 0; loop < device.hw.workItemSize.size(); loop++) {
			out<<device.hw.workItemSize[loop];
			if(loop + 1 < device.hw.workItemSize.size()) out<<", ";
		}
		out<<')'<<endl;
		if(device.img.supported) {
			out<<"2D image WxH="<<device.img.twoMaxW<<'x'<<device.img.twoMaxH<<endl;
			out<<"         R, W="<<device.img.readMax<<", "<<device.img.writeMax<<endl;
			out<<"Max array slices="<<device.img.arraySlicesMax<<endl;
			out<<"1D pixel count from buffer="<<device.img.buffOneDImagePixelsMax<<endl;
		}
		return out.str();
	}
	case ap_description: {
		return GetInfo(initial, ap_vendor) + L", " + GetInfo(initial, ap_name) + L"(" +
		       GetInfo(initial, ap_device) + L",p" + GetInfo(initial, ap_processor) + L",r" + GetInfo(initial, ap_revision) + L")" +
			   L"[" + GetInfo(initial, ap_misc) + L"]";
	}
	}
	throw std::exception("unknown property");
}


asizei OCL20ProcNodes::GetMemorySize(asizei adapter, AdapterMemoryAvailability what) const {
	asizei pindex = 0;
	for(asizei loop = 0; loop < platforms.size(); loop++) {
		if(adapter < platforms[loop].devices.size()) break;
		adapter -= platforms[loop].devices.size();
		pindex++;
	}
	if(pindex >= platforms.size()) throw std::exception("No such device.");
	const Platform &p(platforms[pindex]);
	const Device &device(p.devices[adapter]);
	switch(what) {
		//! \todo this should really be changed! Vidcards with more than 4GiBs are canon already!
	case ama_onboard: return asizei(device.hw.globalMemBytes < 0xFFFFFFFFu? device.hw.globalMemBytes : 0xFFFFFFFFu);
	case ama_system: return 0; // CL does not seem to care about host memory - correctly
	case ama_shared: {
		// perhaps I should check SVM flags? It's not really the same thing.
		return 0; 
	}
	}
	return 0;
}


void OCL20ProcNodes::Mangle(const stratum::WorkUnit &wu) {
	throw std::exception("stub");
}


void OCL20ProcNodes::UseAlgorithm(const char *algo, const char *implementation, const std::vector<Settings::ImplParam> &params) {
	throw std::exception("stub");
}


bool OCL20ProcNodes::SharesFound(std::vector<Nonces> &results) {
	throw std::exception("stub");
}
