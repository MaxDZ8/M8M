/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCommand.h"
#include "../../KnownHardware.h"

namespace commands {
namespace monitor {

template<typename ProcessorProvider>
class SystemInfoCMD : public AbstractCommand {
	ProcessorProvider &procs;
public:
	SystemInfoCMD(ProcessorProvider &processors) : procs(processors), AbstractCommand("systemInfo") { }
	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		// Easy, as all params are ignored.
		using namespace rapidjson;
		build.SetObject();
		build.AddMember("API", StringRef(procs.GetName()), build.GetAllocator());
		build.AddMember("platforms", Value(kArrayType), build.GetAllocator());
		Value &platforms(build["platforms"]);
		platforms.Reserve(procs.platforms.size(), build.GetAllocator());
		for(asizei loop = 0; loop < procs.platforms.size(); loop++) {
			ProcessorProvider::Platform &plat(procs.platforms[loop]);
			Value padd(kObjectType);
#define GS(x) { \
	const std::string temp(procs.GetDeviceGroupInfo(plat, ProcessorProvider::dgis_##x)); \
	padd.AddMember(#x, Value(temp.c_str(), temp.length(), build.GetAllocator()), build.GetAllocator()); \
}
			GS(profile);
			GS(version);
			GS(name);
			GS(vendor);
#undef GS	

			padd.AddMember(StringRef("devices"), Value(kArrayType), build.GetAllocator());
			Value &devices(padd["devices"]);
			devices.Reserve(plat.devices.size(), build.GetAllocator());
			for(asizei dev = 0; dev < plat.devices.size(); dev++) {
				const ProcessorProvider::Device &device(plat.devices[dev]);
				Value add(kObjectType);
#define GDSTR(x) { \
	const std::string temp(procs.GetDeviceInfo(device, ProcessorProvider::dis_##x)); \
	add.AddMember(#x, Value(temp.c_str(), temp.length(), build.GetAllocator()), build.GetAllocator()); \
}
#define GDUI(x)  add.AddMember(#x, Value(procs.GetDeviceInfo(device, ProcessorProvider::diu_##x)), build.GetAllocator());
#define GDUL(x)  add.AddMember(#x, Value(procs.GetDeviceInfo(device, ProcessorProvider::diul_##x)), build.GetAllocator());
#define GDB(x)   add.AddMember(#x, Value(procs.GetDeviceInfo(device, ProcessorProvider::dib_##x)), build.GetAllocator());
				GDSTR(chip);
				GDSTR(vendor);
				GDSTR(driverVersion);
				GDSTR(profile);
				GDSTR(apiVersion);
				GDSTR(extensions);
				GDUI(vendorID);
				GDUI(clusters);
				GDUI(coreClock);
				GDUL(maxMemAlloc);
				GDUL(globalMemBytes);
				GDUL(globalMemCacheBytes);
				GDUL(cbufferBytes);
				GDB(ecc);
				GDB(huma);
				GDB(littleEndian);
				GDB(compiler);
				GDB(linker);
#undef GDB
#undef GDUL
#undef GDUI
#undef GDSTR
				{
					aulong ldsBytes = procs.GetDeviceInfo(device, procs.diul_ldsBytes);
					add.AddMember(StringRef("ldsBytes"), ldsBytes, build.GetAllocator());
					ProcessorProvider::LDSType ldst = procs.GetDeviceLDSType(device);
					switch(ldst) {
					case procs.ldsType_dedicated: add.AddMember("ldsType", Value("dedicated"), build.GetAllocator()); break;
					case procs.ldsType_none: add.AddMember("ldsType", Value("none"), build.GetAllocator()); break;
					case procs.ldsType_global: add.AddMember("ldsType", Value("global"), build.GetAllocator()); break;
					}
				}
				{
					std::string type;
					if(device.type.defaultDevice) type = "default";
					auto append = [&type](const char *text) {
						if(type.size()) type += ',';
						type += text;
					};
					if(device.type.gpu) append("GPU");
					if(device.type.accelerator) append("Accelerator");
					if(device.type.cpu) append("CPU");
					add.AddMember("type", Value(type.c_str(), type.length(), build.GetAllocator()), build.GetAllocator());
				}
				{
					const auint vendor = OpenCL12Wrapper::GetDeviceInfo(device, OpenCL12Wrapper::diu_vendorID);
					const std::string chipName(procs.GetDeviceInfo(device, procs.dis_chip));
					const std::string extensions(procs.GetDeviceInfo(device, procs.dis_extensions));
					KnownHardware::Architecture arch = KnownHardware::GetArchitecture(vendor, chipName.c_str(), extensions.c_str());
					const char *hw = KnownHardware::GetArchPresentationString(arch, false);
					if(hw && strlen(hw)) add.AddMember("arch", Value(hw, strlen(hw), build.GetAllocator()), build.GetAllocator());
				}
				devices.PushBack(add, build.GetAllocator());
			}
			platforms.PushBack(padd, build.GetAllocator());
		}
		return nullptr;
	}
};


}
}
