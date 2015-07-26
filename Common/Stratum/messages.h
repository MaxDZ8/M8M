/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <string>
#include <atomic>
#include "../BTC/structs.h"
#include "../AREN/ArenDataTypes.h"

/*! Stratum is message-based.
Every line is a message or, if you're into marketing buzzword, a RPC call.
Every message is a JSON object which might fall into three categories:
- .id undefined, null, 0 --> notification. Those don't need to be replied so
	they don't need to be tracked.
- else we have two cases
  - .method --> request. The receiving peer must mangle this an produce a...
  - .result --> notification. Result of processing sent to the peer which
	originally produced the request.
The .id subfield is used to pair each method to its result.

There is in fact a third type being the .error. This also comes with
an ID and tells us a request will not receive a response.
The stratum protocol suggests IDs to be arbitrary unique strings.
For the time being, M8M uses unsigned ints. It also appears those
IDs to be separate for requests and results.
That is, a request from server can have the same id as a result from server
as the request-response pairs are resolved according to type-id pairs.
Notice ID generation is not here - this logic is at higher level, those are
mere data containers.

It is unclear whatever messages should have a common base class.
For the time being, they don't.
The only thing the base class could contain is the ID, but this information
is mere detail in the current implementation and those objects have a
considerable different purpose. */
namespace stratum {

/*! According to legacy miner code, this comes in three flavours:
- .params = [],
- .params = [ "versionString" ],
- .params = [ "versionString", "sessionId" ]
Client -> Server, it is the first message sent to initiate connection.
This is a bit special because it doesn't produce a response: this request
is to be sent! */
/*
struct MiningSubscribe {
	const std::string version;
	const std::string session;
	MiningSubscribe() { }
	MiningSubscribe(const char *ver) : version(ver) { }
	MiningSubscribe(const char *ver, const char *ses) : version(ver), session(ses) { }
};

This structure is here for documentation but it's really different from others as it's part of
initialization. It is therefore not exposed - the stratum object takes care of producing an
appropriate message as instructed */


/*! A very simple request, Server -> Client.
While I use ints, the server is not guaranteed to do so. */
struct ClientGetVersionRequest {
	const std::string id; //!< passed by the server
	ClientGetVersionRequest(const std::string &serverid) : id(serverid) { }
};


/*! Server -> Client. The first interesting message with quite some data! */
struct MiningSubscribeResponse {
	std::string sessionID;
	std::vector<unsigned __int8> extraNonceOne;
	__int32 extraNonceTwoSZ;

	//! Remember to populate the extraNonceOne array before using this.
	//! It is not populated in ctor as it requires some more work.
	MiningSubscribeResponse(const std::string &sessid, __int32 extraSZ) : sessionID(sessid), extraNonceTwoSZ(extraSZ) { }
	explicit MiningSubscribeResponse() : extraNonceTwoSZ(0) { }
};


struct MiningNotify {
	/*! The merkle branches to be used to generate the new merlke root are perhaps the most important
	piece of data. Legacy miners fail to process this notification if the roots are not there.
	Due to lack of documentation, it seems I'll also do that.
	The merkle roots are the various array elements in msg.params[4]. */
	std::vector<btc::MerkleRoot> merkles;
	std::string job; //!< msg.params[0], unclear if this is meant to be an int or not
	                 //!< It is in BTC protocol, but I cannot tell about stratum
	std::array<aubyte, 32> prevHash; //!< msg.params[1]
	std::vector<aubyte> coinBaseOne; //!< msg.params[2]
	std::vector<aubyte> coinBaseTwo; //!< msg.params[3]
	auint blockVer; //!< msg.params[5]
	auint nbits;	//!< msg.params[6], network difficulty
	auint ntime;	//!< msg.params[7], always remember ntime is <u>in seconds</u>
	bool clear;		//!< msg.params[8], discard current jobs
	/*! Notice I only set the scalar constants here, got enough params already and memory management
	is better left to some other higher level component. So after ctor call you'll need to set
	this->merkles, this->coinBaseOne, this->coinBaseTwo. It's a bit against RAII but that's it. */
	MiningNotify(const std::string &jobID, __int32 version, __int32 currDiff, __int32 currNTime, bool discardPrev)
	: job(jobID), blockVer(version), nbits(currDiff), ntime(currNTime), clear(discardPrev) {
	}
	explicit MiningNotify() : blockVer(0), nbits(0), ntime(0), clear(false) { }
};

/*! server->client, surprisingly enough, this is parsed as a double,
albeit legacy miners cast it to int in a later stage for visualization purposes.
This also does not require confirmation as it's often sent with no ID so it's a notify. */
struct MiningSetDifficultyNotify {
	double newDiff;
	MiningSetDifficultyNotify(double value) : newDiff(value) { }
};


struct MiningAuthorizeResponse {
	enum AuthorizationResult {
		ar_notRequired, //!< usually p2pool, .result is null
		ar_pass, //!< MPOS, login correct. Password not always checked but not really relevant
		ar_bad //!< MPOS, not accepted
	} authorized;
	MiningAuthorizeResponse(AuthorizationResult status = ar_notRequired) : authorized(status) { }
};


struct MiningSubmitResponse {
	bool accepted;
	MiningSubmitResponse(bool ok = false) : accepted(ok) { }
};

/*

/*
"mining.set_difficulty"
"client.reconnect"
"client.get_version"
"client.show_message"
sprintf(s, "{\"id\": %d, \"method\": \"mining.authorize\", \"params\": [\"%s\", \"%s\"]}",
*/


}
