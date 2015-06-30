/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "M8MIconApp.h"
#include "../Common/PoolInfo.h"
#include "commands/Admin/GetRawConfigCMD.h"
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
class M8MConfiguredApp : public M8MIconApp {
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

private:
    struct CFGLoadInfo {
        std::wstring configFile;
        bool specified = false, redirected = false, valid = false;
    } loadInfo;
    commands::admin::RawConfig config;

    void LoadJSON(const std::wstring &filename) {
	    using std::unique_ptr;
	    using namespace rapidjson;
        const asizei CFG_FILE_MAX_BYTES_ON_ERROR(4096);
	    FILE *jsonFile = nullptr;
	    if(_wfopen_s(&jsonFile, filename.c_str(), L"rb")) return;
	    ScopedFuncCall autoClose([jsonFile]() { fclose(jsonFile); });

	    char jsonReadBuffer[512];
	    FileReadStream jsonIN(jsonFile, jsonReadBuffer, sizeof(jsonReadBuffer));
	    AutoUTFInputStream<unsigned __int32, FileReadStream> input(jsonIN);
	    config.good.ParseStream< 0, AutoUTF<unsigned> >(input);
	    if(config.good.HasParseError()) {
		    config.errOff = config.good.GetErrorOffset();
		    config.errCode = config.good.GetParseError();
		    config.errDesc = GetParseError_En(config.good.GetParseError());
		    _fseeki64(jsonFile, 0, SEEK_END);
		    aulong fileSize = _ftelli64(jsonFile);
		    fileSize = min(fileSize, CFG_FILE_MAX_BYTES_ON_ERROR);
		    std::vector<char> temp(asizei(fileSize) + 1);
		    rewind(jsonFile);
		    if(fread(temp.data(), asizei(fileSize), 1, jsonFile) < 1) throw std::wstring(L"Fatal read error while trying to load \"") + filename + L"\".";
            temp[asizei(fileSize)] = 0;
            config.raw = temp.data();
	    }
    }
    
    static Settings* BuildSettings(std::vector<std::string> &errors, const rapidjson::Value &root, const char *algoSelected) {
	    unique_ptr<Settings> ret(new Settings);
	    if(root.IsObject() == false) {
		    errors.push_back("Valid configurations must be objects.");
		    return nullptr;
	    }
	    using namespace rapidjson;
	    const Value::ConstMemberIterator pools = root.FindMember("pools");
	    auto mkString = [](const Value &jv) { return std::string(jv.GetString(), jv.GetStringLength()); };
	    if(pools == root.MemberEnd()) errors.push_back("No pools specified in config file."); // but keep going, not fatal.
	    else if(pools->value.IsArray() == false) errors.push_back("Pool list must be an array.");
	    else {
            for(SizeType index = 0; index < pools->value.Size(); index++) {
                const Value &load(pools->value[index]);
			    if(!load.IsObject()) {
                    errors.push_back(std::string("pools[") + std::to_string(index) + "] is not an object. Ignored.");
				    continue;
			    }
			    std::string fieldList;
			    bool valid = true;
			    auto reqString = [&fieldList, &valid, &load](const char *key) -> Value::ConstMemberIterator {
				    Value::ConstMemberIterator field = load.FindMember(key);
				    bool good = field != load.MemberEnd() && field->value.IsString();
				    if(!good) {
					    if(fieldList.length()) fieldList += ", ";
					    fieldList += key;
				    }
				    valid &= good;
				    return field;
			    };
			    const auto addr = reqString("url");
			    const auto user = reqString("user");
			    const auto psw = reqString("pass");
			    const auto algo = reqString("algo");
			    if(!valid) {
				    errors.push_back(std::string("pools[") + std::to_string(index) + "] ignored, invalid fields: " + fieldList);
				    continue;
			    }
                const auto poolNick(load.FindMember("name"));
                std::string poolName;
                if(poolNick != load.MemberEnd() && poolNick->value.IsString()) {
                    poolName = mkString(poolNick->value);
                    if(poolName.length() == 0) {
				        errors.push_back("pools[" + std::to_string(index) + "].name is empty string, not allowed");
                        continue;
                    }
                }
                else poolName = std::string("[") + std::to_string(index) + "]";

                auto sameName = std::find_if(ret->pools.cbegin(), ret->pools.cend(), [&poolName](const std::unique_ptr<PoolInfo> &check) {
                    return check->name == poolName;
                });
                if(sameName != ret->pools.cend()) {
                    std::string msg("pools[" + std::to_string(index) + "].name is \"" + poolName + ", already taken by pools[");
                    msg += std::to_string(sameName - ret->pools.cbegin()) + ']';
				    errors.push_back(std::move(msg));
                    continue;
                }

			    unique_ptr<PoolInfo> add(new PoolInfo(poolName, mkString(addr->value), mkString(user->value), mkString(psw->value)));
			    add->algo = mkString(algo->value);
			    const auto proto(load.FindMember("protocol"));
			    const auto diffMul(load.FindMember("diffMultipliers"));
			    const auto merkleMode(load.FindMember("merkleMode"));
			    const auto diffMode(load.FindMember("diffMode"));
			    if(proto != load.MemberEnd() && proto->value.IsString()) add->appLevelProtocol = mkString(proto->value);
			    if(diffMul == load.MemberEnd()) {
				    errors.push_back(std::string("pools[") + std::to_string(index) + "].diffMultipliers not found, old config file?");
				    continue;
			    }
			    else if(diffMul->value.IsObject() == false) {
				    errors.push_back(std::string("pools[") + std::to_string(index) + "].diffMultipliers not an object, old config file?");
				    continue;
			    }
			    else {
				    auto stratum(diffMul->value.FindMember("stratum"));
				    auto one(diffMul->value.FindMember("one"));
				    auto share(diffMul->value.FindMember("share"));
				    auto valid = [&errors, index, &diffMul](adouble &dst, rapidjson::Value::ConstMemberIterator &value) {
					    if(value == diffMul->value.MemberEnd()) {
						    const std::string name(value->name.GetString(), value->name.GetStringLength());
						    errors.push_back(std::string("pools[") + std::to_string(index) + "].diffMultipliers." + name + " missing.");
						    return false;
					    }
					    adouble good = .0;
					    if(value->value.IsUint()) good = adouble(value->value.GetUint());
					    else if(value->value.IsUint64()) good = adouble(value->value.GetUint64());
					    else if(value->value.IsDouble()) good = value->value.GetDouble();
					    if(good == .0 || good < .0) {
						    const std::string name(value->name.GetString(), value->name.GetStringLength());
						    errors.push_back(std::string("pools[") + std::to_string(index) + "].diffMultipliers." + name + " must be number > 0.");
						    return false;
					    }
					    dst = good;
					    return true;
				    };
				    if(!valid(add->diffMul.one, one)) continue;
				    if(!valid(add->diffMul.share, share)) continue;
				    if(!valid(add->diffMul.stratum, stratum)) continue;
			    }
			    if(merkleMode != load.MemberEnd() && merkleMode->value.IsString()) {
				    std::string mmode(mkString(merkleMode->value));
				    if(mmode == "SHA256D") add->merkleMode = PoolInfo::mm_SHA256D;
				    else if(mmode == "singleSHA256") add->merkleMode = PoolInfo::mm_singleSHA256;
				    else throw std::string("Unknown merkle mode: \"" + mmode + "\".");
			    }
			    if(diffMode != load.MemberEnd() && diffMode->value.IsString()) {
				    std::string mmode(mkString(diffMode->value));
				    if(mmode == "btc") add->diffMode = PoolInfo::dm_btc;
				    else if(mmode == "neoScrypt") add->diffMode = PoolInfo::dm_neoScrypt;
				    else throw std::string("Unknown difficulty calculation mode: \"" + mmode + "\".");
			    }
			    ret->pools.push_back(std::move(add));
		    }
		    if(!ret->pools.size()) errors.push_back(std::string("no valid pool configurations!"));
	    }
	    {
		    Value::ConstMemberIterator driver = root.FindMember("driver");
		    Value::ConstMemberIterator defAlgo = root.FindMember("algo");
            Value::ConstMemberIterator reconnDelay = root.FindMember("reconnectDelay");
		    if(driver != root.MemberEnd() && driver->value.IsString()) ret->driver = mkString(driver->value);
            if(algoSelected) ret->algo = algoSelected;
            else if(defAlgo == root.MemberEnd()) {
                throw std::exception("Configuration file missing \"algo\" key, but required when \"--algo\" parameter is not specified.");
            }
            else if(defAlgo->value.IsString() == false) throw std::exception("Invalid configuration, \"algo\" must be a string!");
            else ret->algo = mkString(defAlgo->value);
            if(reconnDelay != root.MemberEnd()) {
                if(reconnDelay->value.IsUint()) ret->reconnDelay = std::chrono::seconds(reconnDelay->value.GetUint());
                else throw std::string("\"reconnectDelay\", value ") + std::to_string(reconnDelay->value.GetUint()) + " is invalid.";
            }
	    }
	    Value::ConstMemberIterator implParams = root.FindMember("implParams");
	    if(implParams != root.MemberEnd()) ret->implParams.CopyFrom(implParams->value, ret->implParams.GetAllocator());
	    return ret.release();
    }
};
