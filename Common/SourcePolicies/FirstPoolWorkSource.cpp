/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "FirstPoolWorkSource.h"


FirstPoolWorkSource::FirstPoolWorkSource(const char *presentation, const AlgoInfo &algoParams, const PoolInfo &init, NetworkInterface::ConnectedSocketInterface &tcpip)
	: AbstractWorkSource(presentation, init.name.c_str(), algoParams, std::make_pair(init.diffMode, init.diffMul), init.merkleMode, PullCredentials(init)),
	  fetching(init), pipe(tcpip), errorCallback(DefaultErrorCallback(false)) {
}


FirstPoolWorkSource::~FirstPoolWorkSource() {
	// Used to destroy the socket, but it's now managed by the external pool manager.
}


void FirstPoolWorkSource::MangleReplyFromServer(size_t id, const rapidjson::Value &result, const rapidjson::Value &error) {
	const char *sent = stratum.Response(id);
	if(error.IsNull() == false) {
		ScopedFuncCall clear([this, id]() { stratum.RequestReplyReceived(id, true); });
		std::string desc;
		aint code;
		if(error.IsArray()) { // MPOS pools give us an array here, first number is an error code int, second is a string.
			const rapidjson::Value &jcode(error[0u]);
			const rapidjson::Value &jdesc(error[1]);
			if(jcode.IsInt()) code = jcode.GetInt();
			else if(jcode.IsUint()) code = static_cast<aint>(jcode.GetUint());
			else throw std::exception("Error code is not 32-bit integer, unsupported.");
			if(jdesc.IsString()) desc.assign(jdesc.GetString(), jdesc.GetStringLength());
		}
		else if(error.IsObject()) { // P2Pools put out messages this way
			const rapidjson::Value &jcode(error["code"]);
			const rapidjson::Value &jdesc(error["message"]);
			if(jcode.IsInt()) code = jcode.GetInt();
			else if(jcode.IsUint()) code = static_cast<aint>(jcode.GetUint());
			else throw std::exception("Error code is not 32-bit integer, unsupported.");
			if(jdesc.IsString()) desc.assign(jdesc.GetString(), jdesc.GetStringLength());
		}
		if(errorCallback) errorCallback(id, code, desc);
	}
	else {
		ScopedFuncCall clear([this, id]() { stratum.RequestReplyReceived(id, false); });
		bool mangled = false;
		using namespace stratum::parsing;
		MangleResult(mangled, sent, id, result, MiningSubscribe());
		MangleResult(mangled, sent, id, result, MiningAuthorize());
		MangleResult(mangled, sent, id, result, MiningSubmit());
		if(!mangled) throw std::exception("unmatched response!");
	}
}


void FirstPoolWorkSource::MangleMessageFromServer(const std::string &idstr, const char *signature, const rapidjson::Value &paramArray) {
	if(paramArray.IsArray() == false) throw std::exception("Notification or requests must have a .param array!");
	bool mangled = false;
	using namespace stratum::parsing;
	MangleNotification(mangled, signature, paramArray, MiningSetDifficulty());
	MangleNotification(mangled, signature, paramArray, MiningNotifyParser());
	MangleRequest(mangled, signature, idstr, paramArray, ClientGetVersion());
	if(!mangled) throw std::exception("unmatched response!");
}


asizei FirstPoolWorkSource::Send(const abyte *data, asizei count) {
	asizei ret = pipe.Send(data, count);
	if(ret == 0 && pipe.Works() == false) throw std::exception("Failed send, socket reset.");
	return ret;
}


asizei FirstPoolWorkSource::Receive(abyte *storage, asizei rem) {
	asizei received = pipe.Receive(storage, rem);
	//if(received < 0) THROW(GetSocketError()); // impossible, now done in the receive call, which is also unsigned
	return received;
}
