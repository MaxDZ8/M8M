/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "FirstPoolWorkSource.h"


FirstPoolWorkSource::FirstPoolWorkSource(const char *presentation, const PoolInfo &init, NetworkInterface::ConnectedSocketInterface &tcpip)
	: AbstractWorkSource(presentation, init.name.c_str(), init.algo.c_str(), init.diffOneMul, init.merkleMode, PullCredentials(init)),
	  fetching(init), pipe(tcpip), errorCallback(DefaultErrorCallback(false)) {
}


FirstPoolWorkSource::~FirstPoolWorkSource() {
	// Used to destroy the socket, but it's now managed by the external pool manager.
}


void FirstPoolWorkSource::MangleReplyFromServer(size_t id, const Json::Value &result, const Json::Value &error) {
	const char *sent = stratum.Response(id);
	if(error.isNull() == false) {
		ScopedFuncCall clear([this, id]() { stratum.RequestReplyReceived(id, true); });
		std::string desc;
		aint code;
		if(error.isArray()) { // MPOS pools instead give us an array here, first number is an error code int, second is a string.
			if(error[0u].isConvertibleTo(Json::ValueType::intValue) == false || error[1].isConvertibleTo(Json::ValueType::stringValue) == false)
				throw std::exception("Ill-formed error message, I don't know how to mangle it.");
			code = error[0u].asInt();
			desc = error[1].asString();
		}
		else if(error.isObject()) { // P2Pools put out messages this way
			if(error["message"].isConvertibleTo(Json::stringValue)) desc = error["message"].asString();
			if(error["code"].isConvertibleTo(Json::intValue)) code = error["code"].asInt();
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


void FirstPoolWorkSource::MangleMessageFromServer(const std::string &idstr, const char *signature, const Json::Value &paramArray) {
	if(paramArray.isArray() == false) throw std::exception("Notification or requests must have a .param array!");
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
