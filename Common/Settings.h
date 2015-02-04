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
	const string user;
	const string pass;
	const string name;
	string algo;
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
		else explicitPort = rem.substr(stop + 1);
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


struct Settings {
	std::vector< unique_ptr<PoolInfo> > pools;
	std::string driver, algo, impl;
	rapidjson::Document implParams;
	bool checkNonces; //!< if this is false, the miner thread will not re-hash nonces and blindly consider them valid

	Settings() : checkNonces(true) { }
};

/*! This structure contains every possible setting, in a way or the other.
On creation, it sets itself to default values - this does not means it'll
produce workable state, some settings such as the pool to mine on cannot 
be reasonably guessed. */
