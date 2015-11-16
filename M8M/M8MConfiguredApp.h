/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "M8MIconApp.h"
#include "../Common/PoolInfo.h"
#include "commands/Admin/GetRawConfigCMD.h"
#include "commands/Admin/ConfigFileCMD.h"
#include <codecvt>
#include <rapidjson/filereadstream.h>
#include <rapidjson/encodedstream.h>
#include <rapidjson/error/en.h>


struct Settings {
	std::vector< unique_ptr<PoolInfo> > pools;
	std::string driver, algo;
    std::chrono::seconds reconnDelay = std::chrono::seconds(120);
	rapidjson::Document implParams;
};


/*! The second thing to bring up the application is to load up the configuration.
This might exist or not, might parse correctly or not and even if parsed, it might be ill-formed.
Anyway, the results of the loading process are to be tracked so the "getRawConfig" and "configFile" commands can get their data. */
class M8MConfiguredApp : public M8MIconApp,
                         public commands::admin::ConfigFileCMD::ConfigInfoProviderInterface {
public:
    Settings* LoadSettings(std::wstring file, bool specified, const char *defAlgo) {
	    using namespace rapidjson;
	    const std::wstring ori(file);
        loadInfo.specified = specified;
        for(asizei attempt = loadInfo.specified? 1 : 0; attempt < 2; attempt++) {
            LoadJSON(file);
            if(config.good.IsObject() == false) break;
            if(specified || attempt) break;
            // else attempt a configuration redirect.
			const Value::ConstMemberIterator redirect = config.good.FindMember("userConfiguration");
			if(redirect == config.good.MemberEnd() || redirect->value.IsString() == false) break;
			else {
                std::string utfbyte(redirect->value.GetString(), redirect->value.GetStringLength());
				std::wstring_convert< std::codecvt_utf8_utf16<wchar_t> > convert;
				file = convert.from_bytes(utfbyte);
                config.valueErrors.clear();
                loadInfo.redirected = true;
			}
        }
        auto configuration(std::make_unique<Settings>());
        if(config.good.IsObject()) loadInfo.valid = true;
        configuration.reset(BuildSettings(config.valueErrors, config.good, defAlgo));
        loadInfo.configFile = std::move(file);
	    loadInfo.redirected = ori != loadInfo.configFile;
	    loadInfo.valid = configuration != nullptr;
        if(!configuration) {
            Popup(L"Error loading configuration.");
            ChangeState(STATE_ERROR, true);
        }
        return configuration.release();
    }

protected:
    struct CFGLoadInfo {
        std::wstring configFile;
        bool specified = false, redirected = false, valid = false;
    } loadInfo;
    commands::admin::RawConfig config;

private:
    void LoadJSON(const std::wstring &filename);

    static Settings* BuildSettings(std::vector<std::string> &errors, const rapidjson::Value &root, const char *algoSelected) {
	    unique_ptr<Settings> ret(new Settings);
	    if(root.IsObject() == false) {
		    errors.push_back("Valid configurations must be objects.");
		    return nullptr;
	    }
	    using namespace rapidjson;
	    const Value::ConstMemberIterator pools = root.FindMember("pools");
	    if(pools == root.MemberEnd()) errors.push_back("No pools specified in config file."); // but keep going, not fatal.
	    else if(pools->value.IsArray() == false) errors.push_back("Pool list must be an array.");
	    else {
            for(SizeType p = 0; p < pools->value.Size(); ++p) {
                auto add { std::move(ParsePool(errors, pools->value[p], p)) };
                if(add) {
                    auto sameName = std::find_if(ret->pools.cbegin(), ret->pools.cend(), [&add](const std::unique_ptr<PoolInfo> &check) {
                        return check->name == add->name;
                    });
                    if(sameName != ret->pools.cend()) {
                        std::string msg("pools[" + std::to_string(p) + "].name is \"" + add->name + ", already taken by pools[");
                        msg += std::to_string(sameName - ret->pools.cbegin()) + ']';
		                errors.push_back(std::move(msg));
                        continue;
                    }
                    ret->pools.push_back(std::move(add));
                }
            }
	        if(!ret->pools.size()) errors.push_back(std::string("no valid pool configurations!"));
        }
	    {
		    Value::ConstMemberIterator driver = root.FindMember("driver");
		    Value::ConstMemberIterator defAlgo = root.FindMember("algo");
            Value::ConstMemberIterator reconnDelay = root.FindMember("reconnectDelay");
		    if(driver != root.MemberEnd() && driver->value.IsString()) ret->driver = MakeString(driver->value);
            if(algoSelected) ret->algo = algoSelected;
            else if(defAlgo == root.MemberEnd()) {
                throw std::exception("Configuration file missing \"algo\" key, but required when \"--algo\" parameter is not specified.");
            }
            else if(defAlgo->value.IsString() == false) throw std::exception("Invalid configuration, \"algo\" must be a string!");
            else ret->algo = MakeString(defAlgo->value);
            if(reconnDelay != root.MemberEnd()) {
                if(reconnDelay->value.IsUint()) ret->reconnDelay = std::chrono::seconds(reconnDelay->value.GetUint());
                else throw std::string("\"reconnectDelay\", value ") + std::to_string(reconnDelay->value.GetUint()) + " is invalid.";
            }
	    }
	    Value::ConstMemberIterator implParams = root.FindMember("implParams");
	    if(implParams != root.MemberEnd()) ret->implParams.CopyFrom(implParams->value, ret->implParams.GetAllocator());
	    return ret.release();
    }

    static std::unique_ptr<PoolInfo> ParsePool(std::vector<std::string> &errors, const rapidjson::Value &poolInfo, asizei index);

    static std::string MakeString(const rapidjson::Value &jv) { return std::string(jv.GetString(), jv.GetStringLength()); }

    // commands::admin::ConfigFileCMD::ConfigInfoProviderInterface //////////////////////////////////////////
    std::wstring Filename() const { return loadInfo.configFile; }
    bool Explicit() const { return loadInfo.specified; }
    bool Redirected() const { return loadInfo.redirected; }
    bool Valid() const { return config.good.IsObject(); }
};
