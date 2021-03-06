/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <rapidjson/document.h>

using std::string;
using std::unique_ptr;

struct PoolInfo {
    string appLevelProtocol; // the initial "stratum+"
    string service; // usually "http". Follows "://", but I'm not including those. If not found, set "http"
    string host; // stuff up to :, if found
    string explicitPort; // I keep it as string, it's used as string in sockets anyway
    struct DiffMultipliers {
        double stratum, one, share;
        DiffMultipliers() : stratum(0), one(0), share(0) { }
    } diffMul;
    string user;
    string pass;
    string name;
    string algo;
    explicit PoolInfo() = default;
    PoolInfo(const string &nick, const string &url, const string &userutf8, const string &passutf8)
        : user(userutf8), pass(passutf8), merkleMode(mm_SHA256D), name(nick), appLevelProtocol("stratum"),
          diffMode(dm_btc) {
        string rem = url.find("stratum+") == 0? url.substr(strlen("stratum+")) : url;
        size_t stop = rem.find("://");
        if(stop < rem.length()) {
            service = rem.substr(0, stop);
            rem = rem.substr(stop + 3);
        }
        else service = "http";
        stop = rem.find(':');
        host = rem.substr(0, stop);
        if(stop == rem.length()) { } // use the service default
        else {
            asizei npos = rem.length() - 1 - stop;
            if(rem[rem.length() - 1] == '/') npos--;
            explicitPort = rem.substr(stop + 1, npos);
        }
    }
    enum MerkleMode {
        mm_SHA256D,
        mm_singleSHA256
    };
    enum DiffMode {
        dm_btc,
        dm_neoScrypt
    };
    MerkleMode merkleMode;
    DiffMode diffMode;
};

/*! \note This could be better located in AlgoSourcesLoader but it ended here for the lack of a better candidate.
Those are supposed to be inferred from other systems as a function of PoolInfo::algo.
In particular, this data was going with AbstractWorkSource and its derived class WorkSource. This is no more the case,
as this information is useful MOSTLY at mining time only and does not really affect anything else... except Work Unit creation
so... pretty much basic information. */
struct CanonicalInfo {
    bool bigEndian;
    aulong diffNumerator;
    explicit CanonicalInfo() : bigEndian(false), diffNumerator(0) { }
};
