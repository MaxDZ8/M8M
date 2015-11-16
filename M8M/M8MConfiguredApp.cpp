#include "M8MConfiguredApp.h"


void M8MConfiguredApp::LoadJSON(const std::wstring &filename) {
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


std::unique_ptr<PoolInfo> M8MConfiguredApp::ParsePool(std::vector<std::string> &errors, const rapidjson::Value &load, asizei index) {
    using namespace rapidjson;
    std::unique_ptr<PoolInfo> empty;
    if(!load.IsObject()) {
        errors.push_back(std::string("pools[") + std::to_string(index) + "] is not an object. Ignored.");
        return empty;
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
        return empty;
    }
    const auto poolNick(load.FindMember("name"));
    std::string poolName;
    if(poolNick != load.MemberEnd() && poolNick->value.IsString()) {
        poolName = MakeString(poolNick->value);
        if(poolName.length() == 0) {
            errors.push_back("pools[" + std::to_string(index) + "].name is empty string, not allowed");
            return empty;
        }
    }
    else poolName = std::string("[") + std::to_string(index) + "]";

    auto add { std::make_unique<PoolInfo>(poolName, MakeString(addr->value), MakeString(user->value), MakeString(psw->value)) };
    add->algo = MakeString(algo->value);
    const auto proto(load.FindMember("protocol"));
    const auto diffMul(load.FindMember("diffMultipliers"));
    const auto merkleMode(load.FindMember("merkleMode"));
    const auto diffMode(load.FindMember("diffMode"));
    if(proto != load.MemberEnd() && proto->value.IsString()) add->appLevelProtocol = MakeString(proto->value);
    if(diffMul == load.MemberEnd()) {
        errors.push_back(std::string("pools[") + std::to_string(index) + "].diffMultipliers not found, old config file?");
        return empty;
    }
    else if(diffMul->value.IsObject() == false) {
        errors.push_back(std::string("pools[") + std::to_string(index) + "].diffMultipliers not an object, old config file?");
        return empty;
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
        if(!valid(add->diffMul.one, one)) return empty;
        if(!valid(add->diffMul.share, share)) return empty;
        if(!valid(add->diffMul.stratum, stratum)) return empty;
    }
    if(merkleMode != load.MemberEnd() && merkleMode->value.IsString()) {
        std::string mmode(MakeString(merkleMode->value));
        if(mmode == "SHA256D") add->merkleMode = PoolInfo::mm_SHA256D;
        else if(mmode == "singleSHA256") add->merkleMode = PoolInfo::mm_singleSHA256;
        else throw std::string("Unknown merkle mode: \"" + mmode + "\".");
    }
    if(diffMode != load.MemberEnd() && diffMode->value.IsString()) {
        std::string mmode(MakeString(diffMode->value));
        if(mmode == "btc") add->diffMode = PoolInfo::dm_btc;
        else if(mmode == "neoScrypt") add->diffMode = PoolInfo::dm_neoScrypt;
        else throw std::string("Unknown difficulty calculation mode: \"" + mmode + "\".");
    }
    return add;
}
