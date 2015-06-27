/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <rapidjson/document.h>
#include <vector>
#include <string>
#include "AbstractAlgorithm.h"


/*! Takes care of parsing algorithm-implementation parameters to known data, checking device compatibility AND creating the actual object. */
class AbstractAlgoFactory {
public:
    /*! This has two goals:
    1- Check validity of the passed object.
    2- Set internal state to be later associated to a device/platform.
    Calling this must be equivalent to resetting the factory somehow. In other terms, calling Parse on this produces the same results on a new object,
    implying factories can be freely reused.
    \return List of encountered errors. */
    virtual std::vector<std::string> Parse(const rapidjson::Value &params) {
        // If here, algorithm requirements are met. Are the settings ok? A typical limitation regards max concurrency.
        std::vector<std::string> ret;
        if(params.IsObject() == false) {
            ret.push_back("Passed parameter structure must be an object.");
            return ret;
        }
	    const rapidjson::Value::ConstMemberIterator li(params.FindMember("linearIntensity"));
        unsigned __int32 linearIntensity = 0;
	    if(li == params.MemberEnd()) {
            ret.push_back("Invalid settings, missing \"linearIntensity\", required.");
            return ret;
        }
        else if(li->value.IsUint()) linearIntensity = li->value.GetUint();
        if(!linearIntensity && li != params.MemberEnd()) ret.push_back("Invalid settings, \"linearIntensity\" cannot be 0.");
        this->linearIntensity = linearIntensity;
        // The nonce must currently be a 32-bit value.
        const asizei hashCount = linearIntensity * GetIntensityMultiplier();
        if(hashCount > auint(~0)) ret.push_back("linearIntensity is too high, would result in more than 4Gi hashes per scan");
        return ret;
    }

    /*! Using the parsed settings, check if the provided device can use the given settings. If not, provide a description of failing metrics.
    A device is said to be Eligible if this function returns empty vector. It can probe both the device itself and its platform.
    \return List of encountered errors. */
    virtual std::vector<std::string> Eligible(cl_platform_id plat, cl_device_id dev) const {
        std::vector<std::string> ret;
        std::vector<char> buff;
        auto profile = GetString(plat, CL_PLATFORM_PROFILE, buff);
        auto version = ExtractVersion(GetString(plat, CL_PLATFORM_VERSION, buff));
        if(profile != "FULL_PROFILE") ret.push_back("Platform must be FULL_PROFILE");
        bool bad = version.first < 1;
        bad |= version.first == 1 && version.second < 2;
        if(bad) ret.push_back("Platform must be at least CL1.2, found " + std::to_string(version.first) + '.' + std::to_string(version.second));

        profile = GetString(dev, CL_DEVICE_PROFILE, buff);
        version = ExtractVersion(GetString(dev, CL_DEVICE_VERSION, buff));
        if(profile != "FULL_PROFILE") ret.push_back("Device must be FULL_PROFILE");
        bad = version.first < 1;
        bad |= version.first == 1 && version.second < 2;
        if(bad) ret.push_back("Device must be at least CL1.2, found " + std::to_string(version.first) + '.' + std::to_string(version.second));
        if((Get<cl_device_type>(dev, CL_DEVICE_TYPE, "error probing device type") & CL_DEVICE_TYPE_GPU) == 0) ret.push_back("Device is not a GPU");
        
        const asizei buffBytes = GetBiggestBufferSize(linearIntensity * GetIntensityMultiplier());
        if(buffBytes > Get<aulong>(dev, CL_DEVICE_MAX_MEM_ALLOC_SIZE, "error probing device max buffer size")) ret.push_back("Biggest buffer exceeds max size");
        // Note: no more rejecting non-AMD_GCN devices.
        return ret;
    }

    virtual void Resources(std::vector<AbstractAlgorithm::ResourceRequest> &res, KnownConstantProvider &K) const = 0;

    virtual void Kernels(std::vector<AbstractAlgorithm::KernelRequest> &kern) const = 0;

    virtual asizei GetHashCount() const = 0;
    virtual asizei GetNumUintsPerCandidate() const = 0;
    virtual SignedAlgoIdentifier GetAlgoIdentifier() const = 0;

protected:
    asizei linearIntensity; //!< I'm pretty sure this one will be common to all algorithms.

    //! How many hashes computed for each linearIntensity increment.
    virtual asizei GetIntensityMultiplier() const = 0;

    //! Returns how much memory in bytes the biggest buffer used will take. Used to compare to max alloc.
    virtual asizei GetBiggestBufferSize(asizei hashCount) const = 0;

    static std::string GetString(cl_platform_id plat, cl_platform_info what, std::vector<char> &temp) {
        asizei size;
        cl_int err = clGetPlatformInfo(plat, what, 0, NULL, &size);
        if(err != CL_SUCCESS) throw std::exception("Some error probing platform string size");
        temp.resize(size);
        err = clGetPlatformInfo(plat, what, temp.size(), temp.data(), NULL);
        if(err != CL_SUCCESS) throw std::exception("Some error probing platform string value");
        return std::string(temp.data(), temp.size() - 1); // includes terminator
    }

    static std::string GetString(cl_device_id dev, cl_platform_info what, std::vector<char> &temp) {
        asizei size;
        cl_int err = clGetDeviceInfo(dev, what, 0, NULL, &size);
        if(err != CL_SUCCESS) throw std::exception("Some error probing device string size");
        temp.resize(size);
        err = clGetDeviceInfo(dev, what, temp.size(), temp.data(), NULL);
        if(err != CL_SUCCESS) throw std::exception("Some error probing device string value");
        return std::string(temp.data(), temp.size() - 1); // includes terminator
    }

    //! Can't believe they still haven't got to clGetPlatformInfo(plat, CL_MAJOR, ...) etc!
    static std::pair<auint, auint> ExtractVersion(const std::string &meh) {
        const char *prefix = "OpenCL ";
        asizei slot = strlen(prefix);
        aint major = atoi(meh.data() + slot);
        if(major < 0) major = 0;
        while(meh[slot] != '.') slot++;
        slot++;
        aint minor = atoi(meh.data() + slot);
        if(minor < 0) minor = 0;
        return std::make_pair(auint(major), auint(minor));
    }

    template<typename SCALAR>
    static SCALAR Get(cl_device_id dev, cl_device_info what, const char *errorMsg) {
        SCALAR ret;
        cl_int err = clGetDeviceInfo(dev, what, sizeof(ret), &ret, NULL);
        if(err != CL_SUCCESS) throw std::exception(errorMsg);
        return ret;
    }
};
