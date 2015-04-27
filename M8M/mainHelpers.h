//! \file Various stuff whose only purpose is to make M8M.cpp smaller
#pragma once
#include "WebMonitorTracker.h"
#include "../Common/NotifyIcon.h"
#include "IconCompositer.h"
#include <rapidjson/document.h>


struct MiniServers {
    WebMonitorTracker monitor;
    WebAdminTracker admin;

    MiniServers(AbstractNotifyIcon &notify, Network &network) : monitor(notify, network), admin(notify, network) { }
    void Init(AbstractNotifyIcon &notify, AbstractIconCompositer &iconBitmaps) {
#if defined (_DEBUG)
        const wchar_t *pathToWebApps = L".." DIR_SEPARATOR L"web" DIR_SEPARATOR;
#else
        const std::wstring pathToWebApps = L"";
#endif
	    monitor.connectClicked = [pathToWebApps]() { 
            const std::wstring webMonitorPath(pathToWebApps + std::wstring(L"monitor_localhost.html"));
            LaunchBrowser(webMonitorPath.c_str());
        };
	    admin.connectClicked = [pathToWebApps]() {
            const std::wstring webAdminPath(pathToWebApps + std::wstring(L"admin_localhost.html"));
            LaunchBrowser(webAdminPath.c_str());
        };
	    admin.clientConnectionCallback = monitor.clientConnectionCallback = [&notify, &iconBitmaps, this](WebMonitorTracker::ClientConnectionEvent ev, aint change, asizei count) {
		    switch(ev) {
		    case WebMonitorTracker::cce_welcome: break;
		    case WebMonitorTracker::cce_farewell: break;
		    }
		    if(totalClients == 0 && change > 0) {
			    std::unique_ptr<aubyte[]> ico(new aubyte[M8M_ICON_SIZE * M8M_ICON_SIZE * 4]);
			    iconBitmaps.SetCurrentIcon(STATE_ICON_CLIENT_CONNECTED);
			    iconBitmaps.GetCompositedIcon(ico.get());
			    notify.SetIcon(ico.get(), M8M_ICON_SIZE, M8M_ICON_SIZE);
		    }
		    else if(totalClients > 0 && change < 0) {
			    std::unique_ptr<aubyte[]> ico(new aubyte[M8M_ICON_SIZE * M8M_ICON_SIZE * 4]);
			    iconBitmaps.SetCurrentIcon(STATE_ICON_LISTENING);
			    iconBitmaps.GetCompositedIcon(ico.get());
			    notify.SetIcon(ico.get(), M8M_ICON_SIZE, M8M_ICON_SIZE);
		    }
		    if(change > 0) totalClients++;
		    else totalClients--;
		    return true;
	    };
		admin.serviceStateCallback = monitor.serviceStateCallback = [&notify, &iconBitmaps, this](bool listening) {
			if(listening) listeningPorts++;
			else listeningPorts--;
			if(listeningPorts == 0 || (listeningPorts == 1 && listening)) {
				std::vector<aubyte> ico;
				iconBitmaps.SetCurrentIcon(listening? STATE_ICON_LISTENING : STATE_ICON_NORMAL);
				iconBitmaps.GetCompositedIcon(ico);
				notify.SetIcon(ico.data(), M8M_ICON_SIZE, M8M_ICON_SIZE);
			}
		};
    }

    bool Nobody() { return listeningPorts == 0; }

private:
    asizei totalClients = 0, listeningPorts = 0;
    // Noncopiable
    MiniServers(const MiniServers &other) = delete;
    MiniServers& operator=(const MiniServers &other) = delete;
};




/*! Helper struct to keep LoadConfigJSON easier to describe. */
struct CFGLoadErrInfo {
	std::unique_ptr<char[]> raw;
	std::string errDesc;
	auint errCode;
	asizei errOff;
};

#define CFG_FILE_MAX_BYTES_ON_ERROR (1024 * 1024)


#include <rapidjson/filereadstream.h>
#include <rapidjson/encodedstream.h>
#include <rapidjson/error/en.h>


/*! A configuration file is a JSON object. This function will try to open the specified filename and load a JSON object from it.
If file cannot be read, it will return false. Even if true is returned, the returned cfg is valid only if an object.
This might be not the case if parsing failed. In this case, errInfo will be properly populated. */
static bool LoadConfigJSON(rapidjson::Document &root, CFGLoadErrInfo &errInfo, const std::wstring &filename) {
	using std::unique_ptr;
	using namespace rapidjson;
	FILE *jsonFile = nullptr;
	if(_wfopen_s(&jsonFile, filename.c_str(), L"rb")) return false;
	ScopedFuncCall autoClose([jsonFile]() { fclose(jsonFile); });

	char jsonReadBuffer[512];
	FileReadStream jsonIN(jsonFile, jsonReadBuffer, sizeof(jsonReadBuffer));
	AutoUTFInputStream<unsigned __int32, FileReadStream> input(jsonIN);
	root.ParseStream< 0, AutoUTF<unsigned> >(input);
	if(root.HasParseError()) {
		errInfo.errOff = root.GetErrorOffset();
		errInfo.errCode = root.GetParseError();
		errInfo.errDesc = GetParseError_En(root.GetParseError());
		_fseeki64(jsonFile, 0, SEEK_END);
		aulong fileSize = _ftelli64(jsonFile);
		fileSize = min(fileSize, CFG_FILE_MAX_BYTES_ON_ERROR);
		errInfo.raw.reset(new char[asizei(fileSize) + 1]);
		errInfo.raw[asizei(fileSize)] = 0;
		rewind(jsonFile);
		if(fread(errInfo.raw.get(), asizei(fileSize), 1, jsonFile) < 1) throw std::wstring(L"Fatal read error while trying to load \"") + filename + L"\".";
	}
	return true;
}


struct Settings {
	std::vector< unique_ptr<PoolInfo> > pools;
	std::string driver, algo, impl;
	rapidjson::Document implParams;
	bool checkNonces; //!< if this is false, the miner thread will not re-hash nonces and blindly consider them valid

	Settings() : checkNonces(true) { }
};
/*!< This structure contains every possible setting, in a way or the other.
On creation, it sets itself to default values - this does not means it'll
produce workable state, some settings such as the pool to mine on cannot 
be reasonably guessed. */


static Settings* BuildSettings(std::vector<std::string> &errors, const rapidjson::Value &root) {
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
		Value::ConstMemberIterator algo = root.FindMember("algo");
		Value::ConstMemberIterator impl = root.FindMember("impl");
		if(driver != root.MemberEnd() && driver->value.IsString()) ret->driver = mkString(driver->value);
		if(algo != root.MemberEnd() && algo->value.IsString()) ret->algo = mkString(algo->value);
		if(impl != root.MemberEnd() && impl->value.IsString()) ret->impl = mkString(impl->value);
		Value::ConstMemberIterator checkNonces = root.FindMember("checkNonces");
		if(checkNonces != root.MemberEnd() && checkNonces->value.IsBool()) ret->checkNonces = checkNonces->value.GetBool();
	}
	Value::ConstMemberIterator implParams = root.FindMember("implParams");
	if(implParams != root.MemberEnd()) ret->implParams.CopyFrom(implParams->value, ret->implParams.GetAllocator());
	return ret.release();
}


static std::string Suffixed(unsigned __int64 value) {
	using namespace std;
	if(value < 1000) return to_string(value);
	const char *suffix = "KMGTPEZY";
	unsigned int part = value % 1000;
	value /= 1000;
	size_t iso = 0;
	while(iso < strlen(suffix) - 1 && value >= 1000) {
		part = value % 1000;
		value /= 1000;
		iso++;
	}
	auto ret(to_string(value));
	if(value < 100) {
		ret += '.';
		if(value > 10 || part >= 100) part /= 10;
		else if(part < 100) {
			ret += '0'; // if here, we divided at least once
			part /= 10; // and eat a digit away
		}
		ret += to_string(part / (value >= 10? 10 : 1));
	}
	return ret + suffix[iso];
}


/*! This is meant to be similar to legacy miners suffix_string_double function. SIMILAR. Not the same but hopefully compatible to parsers.
The original is fairly complex in behaviour and it's pretty obvious that was not intended. This one works in an hopefully simplier way.
It tries to encode a number in a string counting 5 chars.
1) Numbers below 100 get as many digits after decimal separator as they can to fill so you get 4 digits.
2) Numbers between 100 and 1000 produce integers as the deviation is considered acceptable. So you get 3 digits.
3) When an iso suffix can be applied that will consider the highest three digits and work similarly but you'll only get 3 digits total
	so, counting the separator they'll still be 5 chars unless the value is still big enough to not require decimals (2) so you'll get 4 chars total. */
static std::string Suffixed(double value) {
	using namespace std;
	if(value < 100.0) {
		stringstream build;
		build.precision(3 - (value >= 10.0? 1 : 0));
		build<<fixed<<value;
		return build.str();
	}
	// In all other cases, we have sufficient digits to be accurate with integers.
	return Suffixed(static_cast<unsigned __int64>(value)); // truncate to be conservative
}


#include <ctime>


struct ShareIdentifier {
    const AbstractWorkSource *owner;
    asizei poolIndex;
    asizei shareIndex;

    bool operator<(const ShareIdentifier &other) const {
        return poolIndex < other.poolIndex || shareIndex < other.shareIndex;
    }
};


struct ShareFeedbackData {
    adouble shareDiff, targetDiff;
    bool block;
    asizei gpuIndex;
    std::array<unsigned char, 4> hashSlice;
};


static void ShareFeedback(const ShareIdentifier &share, const ShareFeedbackData &data, StratumShareResponse response) {
    using namespace std;
    //   H   M   S   :       []<space>
    char timeString[2 + 2 + 2 + 2 + 1 + 3];
    auto sinceEpoch = time(NULL);
    tm now;
    gmtime_s(&now, &sinceEpoch);
    strftime(timeString, sizeof(timeString), "[%H:%M:%S] ", &now);

    char hashPart[9];
    const char *hex = "0123456789abcdef";
    for(asizei loop = 0; loop < 4; loop++) {
        hashPart[loop * 2 + 0] = hex[data.hashSlice[loop] >> 4];
        hashPart[loop * 2 + 1] = hex[data.hashSlice[loop] & 0x0F];
    }
    hashPart[8] = 0;

    stringstream diff;
    const std::string poolIdentifier(share.owner->name.length()? share.owner->name : ('[' + std::to_string(share.poolIndex) + ']'));
    if(response == ssr_expired) {
        cout<<"Share "<<share.shareIndex<<" sent to pool "<<poolIdentifier<<" has been dropped. No response from server."<<endl;
    }
    else {
        const char *reply = response == ssr_accepted? "Accepted" : "Rejected";
        cout<<timeString<<reply<<' '<<hashPart<<" Diff "<<Suffixed(data.shareDiff)<<'/'<<Suffixed(data.targetDiff);
        if(data.block) cout<<" BLOCK!";
        cout<<" GPU "<<data.gpuIndex<<std::endl;

    }
}
