/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCommand.h"
#include <json/json.h>
#include "../../KnownHardware.h"

namespace commands {
namespace monitor {

template<typename ProcessorProvider>
class SystemCMD : public AbstractCommand {
	ProcessorProvider &procs;
public:
	SystemCMD(ProcessorProvider &processors) : procs(processors), AbstractCommand("system?") { }
	PushInterface* Parse(Json::Value &build, const Json::Value &input) {
		// Easy, as all params are ignored.
		build = Json::Value(Json::objectValue);
		build["API"] = procs.GetName();
		build["platforms"] = Json::Value(Json::arrayValue);
		Json::Value &platforms(build["platforms"]);
		for(asizei loop = 0; loop < procs.platforms.size(); loop++) {
			ProcessorProvider::Platform &plat(procs.platforms[loop]);
			platforms[loop] = Json::Value(Json::objectValue);
#define GS(x) platforms[loop][#x] = procs.GetDeviceGroupInfo(plat, ProcessorProvider::dgis_##x);
			GS(profile);
			GS(version);
			GS(name);
			GS(vendor);
#undef GS	

			platforms[loop]["devices"] = Json::Value(Json::arrayValue);
			Json::Value &devices(platforms[loop]["devices"]);
			for(asizei dev = 0; dev < plat.devices.size(); dev++) {
				const ProcessorProvider::Device &device(plat.devices[dev]);
				Json::Value add(Json::objectValue);
#define GDSTR(x) add[#x] = procs.GetDeviceInfo(device, ProcessorProvider::dis_##x);
#define GDUI(x)  add[#x] = procs.GetDeviceInfo(device, ProcessorProvider::diu_##x);
#define GDUL(x)  add[#x] = auint(min(procs.GetDeviceInfo(device, ProcessorProvider::diul_##x), auint(-1))); //!< todo get the rid of jsoncpp and go to rapidjson, this is bullshit
#define GDB(x)   add[#x] = procs.GetDeviceInfo(device, ProcessorProvider::dib_##x);
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
					add["ldsBytes"] = auint(min(ldsBytes, auint(-1)));
					ProcessorProvider::LDSType ldst = procs.GetDeviceLDSType(device);
					switch(ldst) {
					case procs.ldsType_dedicated: add["ldsType"] = "dedicated"; break;
					case procs.ldsType_none: add["ldsType"] = "none"; break;
					case procs.ldsType_global: add["ldsType"] = "global";
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
					add["type"] = type;
				}
				{
					const auint vendor = OpenCL12Wrapper::GetDeviceInfo(device, OpenCL12Wrapper::diu_vendorID);
					const std::string chipName(procs.GetDeviceInfo(device, procs.dis_chip));
					const char *hw = KnownHardware::GetArchitecture(vendor, chipName.c_str());
					if(hw && strlen(hw)) add["arch"] = hw;
				}
				devices[dev] = add;
			}
		}
		return nullptr;
	}
};


}
}
