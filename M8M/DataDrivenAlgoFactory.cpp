/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "DataDrivenAlgoFactory.h"


void DataDrivenAlgoFactory::Resources(std::vector<AbstractAlgorithm::ResourceRequest> &res, KnownConstantProvider &K) const {
    res.resize(resources.size());
    for(asizei cp = 0; cp < resources.size(); cp++) {
        res[cp] = resources[cp]; // slice'em
        if(resources[cp].linearIndex) {
            const asizei linear = linearSize[res[cp].bytes].second;
            res[cp].bytes = GetHashCount() * linear;
        }
    }
}


bool DataDrivenAlgoFactory::ParseSpecial(const rapidjson::Value &arr, std::string &name, KnownConstantProvider &ck) {
    // auint &mulIndex(footprintAdjustment[slot]);
    if(name[0] != '$') return false;
    if(arr.IsArray() == false) return false;
    name = name.c_str() + 1;
    CryptoConstant selected(CryptoConstant::AES_T);
    MetaResource target;
    if(name == "AES_T_TABLES") target.presentationName = "AES T tables";
    else if(name == "SIMD_ALPHA") { selected = CryptoConstant::SIMD_alpha;    target.presentationName = "SIMD &alpha; table"; }
    else if(name == "SIMD_BETA") { selected = CryptoConstant::SIMD_beta;    target.presentationName = "SIMD &beta; table"; }
    else throw std::string("Unknown special constant \"") + name + '"';
    auto known(ck.GetPrecomputedConstant(selected));

    target.bytes = known.second;
    target.initialData = known.first;
    target.name = std::move(name);
    target.memFlags = ParseMemFlags(arr[0u]);
    target.memFlags |= CL_MEM_COPY_HOST_PTR;
    target.useProvidedBuffer = true;
    resources.push_back(target);
    return true;
}

cl_mem_flags DataDrivenAlgoFactory::ParseMemFlags(const rapidjson::Value &string) {
    std::vector<char> complete(string.GetStringLength() + 1);
    asizei dst = 0;
    for(asizei src = 0; src < complete.size() - 1; src++) {
        auto c = string.GetString()[src];
        if(c > 32) complete[dst++] = c;
    }

    std::vector<std::string> parts;
    char *ptr = complete.data();
    char *begin = ptr;
    while(*ptr) {
        if(*ptr == ',') {
            *ptr = 0;
            parts.push_back(std::string(begin));
            *ptr = ','; // for debug mostly
            begin = ptr + 1;
        }
        ptr++;
    }
    parts.push_back(std::string(begin));
    cl_mem_flags ret = 0;
    for(const auto &el : parts) {
        if(_stricmp(el.c_str(), "gpu_only") == 0) ret |= CL_MEM_HOST_NO_ACCESS;
        else if(_stricmp(el.c_str(), "ro") == 0) ret |= CL_MEM_READ_ONLY;
        else if(_stricmp(el.c_str(), "wo") == 0) ret |= CL_MEM_WRITE_ONLY;
        else if(_stricmp(el.c_str(), "host_memory") == 0) ret |= CL_MEM_USE_HOST_PTR;
        else if(_stricmp(el.c_str(), "host_alloc") == 0) ret |= CL_MEM_ALLOC_HOST_PTR;
        else if(_stricmp(el.c_str(), "host_wo") == 0) ret |= CL_MEM_HOST_WRITE_ONLY;
        else if(_stricmp(el.c_str(), "host_ro") == 0) ret |= CL_MEM_HOST_READ_ONLY;
        else throw std::string("Unknown memory property \"") + el + '"';
    }

    auto set = [ret](cl_bitfield flag) { return (ret & flag) != 0; };
    if(set(CL_MEM_HOST_NO_ACCESS) && (set(CL_MEM_HOST_WRITE_ONLY) || set(CL_MEM_HOST_READ_ONLY))) throw std::exception("Incoherent host access flags.");
    if(set(CL_MEM_HOST_WRITE_ONLY) && set(CL_MEM_HOST_READ_ONLY)) throw std::exception("Conflicting host access flags.");
    if(set(CL_MEM_READ_ONLY) && set(CL_MEM_WRITE_ONLY)) throw std::exception("Conflicting device access flags.");
    if(set(CL_MEM_USE_HOST_PTR) && set(CL_MEM_ALLOC_HOST_PTR)) throw std::exception("Conflicting buffer allocation flags.");
    return ret;
}


bool DataDrivenAlgoFactory::ParseImmediate(const rapidjson::Value &arr, std::string &name) {
    asizei limit = 0;
    while(name[limit] && name[limit] > 32) limit++;
    if(name[limit] != 32) return false; // that is, immediates are recognized because they have a space somewhere.
    std::string type(name.substr(0, limit));
    while(name[limit] && name[limit] <= 32) limit++;
    name = name.c_str() + limit;
    auint dimensions = 1;
    asizei square = type.find('[');
    if(square < type.length()) {
        limit = square;
        while(type[limit] && type[limit] != ']') limit++;
        if(type[limit] != ']') throw std::exception("Invalid immediate array specification!");
        for(asizei check = square + 1; check < limit; check++) {
            bool digit = type[check] >= '0' && type[check] <= '9';
            if(!digit) throw std::exception("Invalid immediate array specification, number of entries must be positive integer!");
        }
        int dims = atoi(type.c_str() + square + 1);
        if(dims <= 0) throw std::string("Invalid, immediate array with size ") + std::to_string(dims);
        dimensions = auint(dims);
        type = type.substr(0, square);
    }
    limit = 0;
    while(name[limit] && name[limit] <= 32) limit++;
    name = name.c_str() + limit;

    static std::map<std::string, asizei> footprint;
    if(footprint.empty()) {
        footprint.insert(std::make_pair("uint", 4));
        footprint.insert(std::make_pair("int", 4));
        footprint.insert(std::make_pair("single", 4));
        footprint.insert(std::make_pair("double", 8));
        footprint.insert(std::make_pair("long", 4));
        footprint.insert(std::make_pair("ulong", 4));
    }
    auto match(footprint.find(type));
    if(match == footprint.end()) throw std::string("Unrecognized immediate type \"") + type + '"';

    MetaResource target;
    if(dimensions == 1) { // scalars do fit.
        target.bytes = match->second;
        ParseValue(target.imValue, sizeof(target.imValue), type, arr, name);
        target.immediate = true;
    }
    else {
        if(arr.IsArray() == false) throw std::string("Immediate \"") + name + '"' + " should be an array";
        if(dimensions != arr.Size()) throw std::string("Immediate \"") + name + '"' + " declared counting " + std::to_string(dimensions) + " entries but got " + std::to_string(arr.Size());
        std::pair<asizei, std::unique_ptr<aubyte[]>> add;
        add.first = target.bytes = match->second * dimensions;
        add.second.reset(new aubyte[add.first]);
        persistentBlobs.push_back(std::move(add));
        auto *dst = persistentBlobs.back().second.get();
        auto rem = persistentBlobs.back().first;
        target.initialData = dst;
        for(auto i = arr.Begin(); i != arr.End(); ++i) {
            ParseValue(dst, rem, type, *i, name);
            dst += match->second;
            rem -= match->second;
        }
        target.memFlags |= CL_MEM_COPY_HOST_PTR | CL_MEM_READ_ONLY | CL_MEM_HOST_NO_ACCESS;
    }
    target.name = std::move(name);
    resources.push_back(target);
    return true;
}


void DataDrivenAlgoFactory::ParseValue(aubyte *dst, asizei rem, const std::string &type, const rapidjson::Value &im, const std::string &name) {
    if(type == "uint") {
        if(im.IsUint() == false) throw std::string("Typed immediate ") + name + " is not uint ";
        auint value = im.GetUint();
        memcpy_s(dst, rem, &value, sizeof(value));
    }
    else if(type == "int") {
        if(im.IsInt() == false) throw std::string("Typed immediate ") + name + " is not int ";
        aint value = im.GetInt();
        memcpy_s(dst, rem, &value, sizeof(value));
    }
    else if(type == "ulong") {
        if(im.IsUint64() == false) throw std::string("Typed immediate ") + name + " is not ulong ";
        aulong value = im.GetUint64();
        memcpy_s(dst, rem, &value, sizeof(value));
    }
    else if(type == "long") {
        if(im.IsInt64() == false) throw std::string("Typed immediate ") + name + " is not long ";
        along value = im.GetInt64();
        memcpy_s(dst, rem, &value, sizeof(value));
    }
    else if(type == "single" || type == "double") {
        if(im.IsDouble() == false) throw std::string("Typed immediate ") + name + " is not a floating point value";
        adouble value = im.GetDouble();
        if(type == "double") memcpy_s(dst, rem, &value, sizeof(value));
        else {
            asingle singlePrecision = asingle(value);
            memcpy_s(dst, rem, &singlePrecision, sizeof(singlePrecision));
        }
    }
    else throw std::exception("Unknown type to unpack - code incoherent"); // impossible
}


void DataDrivenAlgoFactory::ParseCustom(const rapidjson::Value &arr, std::string &name) {
    rapidjson::SizeType p = 0;
    MetaResource target;
    target.memFlags = ParseMemFlags(arr[p]);
    p++;
    if(arr[p].IsString()) {
        std::string size(arr[1].GetString(), arr[1].GetStringLength());
        auto match(std::find_if(linearSize.cbegin(), linearSize.cend(), [&size](const std::pair<std::string, auint> &test) {
            return test.first == size;
        }));
        if(match == linearSize.cend()) throw std::string("Could not find size \"") + size + '"';
        target.bytes = match - linearSize.cbegin();
        target.linearIndex = true;
    }
    else if(arr[p].IsUint()) {
        target.bytes = arr[p].GetUint();
        // todo: extract byte blob?
        // p++;
    }
    p++;

    if(p < arr.Size()) {
        if(arr[p].IsString() == false) throw std::exception("Presentation string expected but non-string provided.");
        target.presentationName = std::string(arr[p].GetString(), arr[p].GetStringLength());
    }
    target.name = std::move(name);
    resources.push_back(target);
}


AbstractAlgorithm::WorkGroupDimensionality DataDrivenAlgoFactory::ParseGroupSize(const rapidjson::Value &arr) {
    if(arr.IsArray() == false) throw std::exception("Work dimensionality must be an array.");
    if(arr.Size() < 1 || arr.Size() > 3) throw std::exception("Work dimensionality must count 1-3 entries.");
    AbstractAlgorithm::WorkGroupDimensionality ret;
    ret.dimensionality = arr.Size();
    for(rapidjson::SizeType i = 0; i < arr.Size(); i++) {
        if(arr[i].IsUint() == false) throw std::exception("Work dimensionality must be uint");
        if(arr[i].GetUint() == 0) throw std::exception("Work dimensionality must always be > 0");
        ret.wgs[i] = arr[i].GetUint();
    }
    return ret;
}
