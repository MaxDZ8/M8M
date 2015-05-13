/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "WorkSource.h"


bool WorkSource::Use(NetworkInterface::ConnectedSocketInterface *remote) {
    if(credentials.empty()) return false;
    pipe = remote;
    std::vector<std::pair<const char*, const char*>> meh(credentials.size());
    for(asizei cp = 0; cp < credentials.size(); cp++) {
        meh[cp].first = credentials[cp].first.c_str();
        meh[cp].second = credentials[cp].second.c_str();
    }
    NewStratum(meh);
    return true;
}


void WorkSource::Disconnected() {
    this->ClearStratum();
    pipe = nullptr;
}


void WorkSource::MangleReplyFromServer(size_t id, const rapidjson::Value &result, const rapidjson::Value &error) {
	const char *sent = stratum->Response(id);
	if(error.IsNull() == false) {
		ScopedFuncCall clear([this, id]() { stratum->RequestReplyReceived(id, true); });
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
		if(errorCallback) errorCallback(*this, id, code, desc);
	}
	else {
		ScopedFuncCall clear([this, id]() { stratum->RequestReplyReceived(id, false); });
		bool mangled = false;
		using namespace stratum::parsing;
		MangleResult(mangled, sent, id, result, MiningSubscribe());
		MangleResult(mangled, sent, id, result, MiningAuthorize());
		MangleResult(mangled, sent, id, result, MiningSubmit());
		if(!mangled) throw std::exception("unmatched response!");
	}
}


void WorkSource::MangleMessageFromServer(const std::string &idstr, const char *signature, const rapidjson::Value &paramArray) {
	if(paramArray.IsArray() == false) throw std::exception("Notification or requests must have a .param array!");
	bool mangled = false;
	using namespace stratum::parsing;
	MangleNotification(mangled, signature, paramArray, MiningSetDifficulty());
	MangleNotification(mangled, signature, paramArray, MiningNotifyParser());
	MangleRequest(mangled, signature, idstr, paramArray, ClientGetVersion());
	if(!mangled) throw std::exception("unmatched response!");
}


std::pair<bool, asizei> WorkSource::Send(const abyte *data, asizei count) {
	asizei sent = pipe->Send(data, count);
    auto pair(std::make_pair(true, sent));
	if(sent == 0 && pipe->Works() == false) pair.first = false;
	return pair;
}


std::pair<bool, asizei> WorkSource::Receive(abyte *storage, asizei rem) {
	asizei received = pipe->Receive(storage, rem);
    auto pair(std::make_pair(true, received));
    if(received == 0 && pipe->Works() == false) pair.first = false;
    return pair;
}


void WorkSource::GetCredentials(std::vector< std::pair<const char*, StratumState::AuthStatus> > &list) const {
    for(auto &worker : credentials) list.push_back(std::make_pair(worker.first.c_str(), StratumState::as_off));
}
