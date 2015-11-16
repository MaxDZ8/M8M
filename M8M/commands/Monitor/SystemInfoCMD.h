/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCommand.h"
#include "../../KnownHardware.h"

namespace commands {
namespace monitor {


class SystemInfoCMD : public AbstractCommand {
public:
    struct ProcessingNodesEnumeratorInterface {
        virtual const char* GetAPIName() const = 0;
        virtual asizei GetNumPlatforms() const = 0;

	    enum PlatformInfoString {
		    pis_profile,
		    pis_version,
		    pis_name,
		    pis_vendor,
		    pis_extensions
	    };
        virtual std::string GetPlatformString(asizei p, PlatformInfoString pis) const = 0;
        virtual asizei GetNumDevices(asizei p) const = 0;

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
	    virtual std::string GetDeviceInfo(asizei plat, asizei dev, DeviceInfoString prop) const = 0;
	    virtual auint GetDeviceInfo(asizei plat, asizei dev, DeviceInfoUint prop) = 0;
	    virtual aulong GetDeviceInfo(asizei plat, asizei dev, DeviceInfoUnsignedLong prop) = 0;
	    virtual bool GetDeviceInfo(asizei plat, asizei dev, DeviceInfoBool prop) = 0;
	    enum LDSType {
		    ldsType_none,
		    ldsType_global,
		    ldsType_dedicated
	    };
	    virtual LDSType GetDeviceLDSType(asizei plat, asizei dev) = 0;
        struct DevType {
            bool defaultDevice = false;
            bool cpu = false;
            bool gpu = false;
            bool accelerator = false;
        };
        virtual DevType GetDeviceType(asizei plat, asizei dev) = 0;
    };

	SystemInfoCMD(ProcessingNodesEnumeratorInterface &processors) : procs(processors), AbstractCommand("systemInfo") { }
	PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
		// Easy, as all params are ignored.
		using namespace rapidjson;
		build.SetObject();
		build.AddMember("API", StringRef(procs.GetAPIName()), build.GetAllocator());
		build.AddMember("platforms", Value(kArrayType), build.GetAllocator());
		Value &platforms(build["platforms"]);
		platforms.Reserve(rapidjson::SizeType(procs.GetNumPlatforms()), build.GetAllocator());
		for(asizei plat = 0; plat < procs.GetNumPlatforms(); plat++) {
			Value padd(kObjectType);
#define GS(x) { \
	const std::string temp(procs.GetPlatformString(plat, ProcessingNodesEnumeratorInterface::pis_##x)); \
	padd.AddMember(#x, Value(temp.c_str(), rapidjson::SizeType(temp.length()), build.GetAllocator()), build.GetAllocator()); \
}
			GS(profile);
			GS(version);
			GS(name);
			GS(vendor);
#undef GS

			padd.AddMember(StringRef("devices"), Value(kArrayType), build.GetAllocator());
			Value &devices(padd["devices"]);
			devices.Reserve(rapidjson::SizeType(procs.GetNumDevices(plat)), build.GetAllocator());
			for(asizei dev = 0; dev < procs.GetNumDevices(plat); dev++) {
                devices.PushBack(Populate(plat, dev, build.GetAllocator()), build.GetAllocator());
            }
			platforms.PushBack(padd, build.GetAllocator());
		}
		return nullptr;
	}
private:
    ProcessingNodesEnumeratorInterface &procs;

    rapidjson::Value Populate(asizei plat, asizei dev, rapidjson::MemoryPoolAllocator<> &alloc) {
        using namespace rapidjson;
        Value add(kObjectType);
#define GDSTR(x) { \
	const std::string temp(procs.GetDeviceInfo(plat, dev, ProcessingNodesEnumeratorInterface::dis_##x)); \
	add.AddMember(#x, Value(temp.c_str(), rapidjson::SizeType(temp.length()), alloc), alloc); \
}
#define GDUI(x)  add.AddMember(#x, Value(procs.GetDeviceInfo(plat, dev, ProcessingNodesEnumeratorInterface::diu_##x)), alloc);
#define GDUL(x)  add.AddMember(#x, Value(procs.GetDeviceInfo(plat, dev, ProcessingNodesEnumeratorInterface::diul_##x)), alloc);
#define GDB(x)   add.AddMember(#x, Value(procs.GetDeviceInfo(plat, dev, ProcessingNodesEnumeratorInterface::dib_##x)), alloc);
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
			aulong ldsBytes = procs.GetDeviceInfo(plat, dev, procs.diul_ldsBytes);
			add.AddMember(StringRef("ldsBytes"), ldsBytes, alloc);
			ProcessingNodesEnumeratorInterface::LDSType ldst = procs.GetDeviceLDSType(plat, dev);
			switch(ldst) {
			case ProcessingNodesEnumeratorInterface::ldsType_dedicated: add.AddMember("ldsType", Value("dedicated"), alloc); break;
			case ProcessingNodesEnumeratorInterface::ldsType_none: add.AddMember("ldsType", Value("none"), alloc); break;
			case ProcessingNodesEnumeratorInterface::ldsType_global: add.AddMember("ldsType", Value("global"), alloc); break;
			}
		}
        auto device(procs.GetDeviceType(plat, dev));
		{
			std::string type;
			if(device.defaultDevice) type = "default";
			auto append = [&type](const char *text) {
				if(type.size()) type += ',';
				type += text;
			};
			if(device.gpu) append("GPU");
			if(device.accelerator) append("Accelerator");
			if(device.cpu) append("CPU");
			add.AddMember("type", Value(type.c_str(), rapidjson::SizeType(type.length()), alloc), alloc);
		}
		{
			const auint vendor = procs.GetDeviceInfo(plat, dev, ProcessingNodesEnumeratorInterface::diu_vendorID);
			const std::string chipName(procs.GetDeviceInfo(plat, dev, procs.dis_chip));
			const std::string extensions(procs.GetDeviceInfo(plat, dev, procs.dis_extensions));
			const bool cpu { device.gpu == false };
			auto hw = knownHardware::GetArch(vendor, cpu, device.gpu, chipName, extensions.c_str()).GetPresentationString(false);
			if(hw && strlen(hw)) add.AddMember("arch", Value(hw, rapidjson::SizeType(strlen(hw)), alloc), alloc);
		}
       return add;
    }
};


}
}
