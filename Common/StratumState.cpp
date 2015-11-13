/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "StratumState.h"


asizei StratumState::PushMethod(const char *method, const string &pairs) {
	asizei used = nextRequestID++;
	string idstr("{");
	idstr += "\"id\": \"" + std::to_string(used) + "\", ";
	idstr += "\"method\": \"";
	idstr += method;
	idstr += "\", ";
	idstr += pairs + "}\n";
	Blob add(idstr.c_str(), idstr.length(), used);
	pendingRequests.insert(std::make_pair(used, method));
	ScopedFuncCall pullout([used, this] { pendingRequests.erase(used); });
	pending.push(std::move(add));

	pullout.Dont();
	if(!nextRequestID) nextRequestID++; // we must have been running like one hundred years I guess
	/* this was the initialization message. Regardless of when the server will
	send the message, consider this to be initialization time. */
	return used;
}


void StratumState::PushResponse(const string &serverid, const string &pairs) {
	string idstr("{");
	idstr += "\"id\": \"" + serverid + "\",";
	idstr += pairs + "}\n";
	Blob add(idstr.c_str(), idstr.length(), 0); // we don't need to track responses so just give them id 0
	pending.push(std::move(add));
}


StratumState::StratumState()
	: nextRequestID(1), difficulty(.0), errorCount(0) {
	dataTimestamp = 0;
	size_t used = PushMethod("mining.subscribe", KeyValue("params", "[]", false));
	ScopedFuncCall pop([this]() { this->pending.pop(); });
	pendingRequests.insert(std::make_pair(used, "mining.subscribe"));
	pop.Dont();
}


void StratumState::Authorize(const char *user, const char *psw) {
	if(std::find_if(workers.cbegin(), workers.cend(), [user](const Worker &test) { return test.name == user; }) != workers.cend()) return;
	std::string identification("[\"");
	identification += user;
	identification += "\", \"";
	identification += psw;
	identification += "\"]";
	asizei used = PushMethod("mining.authorize", KeyValue("params", identification, false));
	ScopedFuncCall popMsg([this]() { this->pending.pop(); });
	pendingRequests.insert(std::make_pair(used, "mining.authorize"));
	ScopedFuncCall popPending([this, used]() { this->pendingRequests.erase(used); });
	workers.push_back(Worker(user, used));
	popPending.Dont();
	popMsg.Dont();
    if(workerAuthCallback) workerAuthCallback(workers.back().name, as_pending);
}


bool StratumState::IsWorker(const char *name) const {
	auto match(std::find_if(workers.cbegin(), workers.cend(), [name](const Worker &test) { return test.name == name; }));
	return match == workers.cend()? false : true;
}


StratumState::AuthStatus StratumState::GetAuthenticationStatus(const char *name) const {
	auto match(std::find_if(workers.cbegin(), workers.cend(), [name](const Worker &test) { return test.name == name; }));
	if(match == workers.cend()) return as_failed;
	return match->authStatus;
}


asizei StratumState::SendWork(const std::string &job, auint ntime, auint nonce2, auint nonce) {
	const char *user = workers[0].name.c_str(); //!< \todo quick hack to match, should be tracked by worker!
	auto &worker(workers[0]);

	const char *sep = "\", \"";
	std::stringstream params;
	params<<std::hex;
	params<<"[\""<<worker.name<<sep<<job<<sep
		  <<std::setw(8)<<std::setfill('0')<<nonce2<<sep
		  <<std::setw(8)<<std::setfill('0')<<ntime<<sep
		  <<std::setw(8)<<std::setfill('0')<<HTON(nonce)<<"\"]";
	const std::string message(params.str());
	const size_t used = PushMethod("mining.submit", KeyValue("params", message, false));
	ScopedFuncCall popMsg([this]() { this->pending.pop(); });
	submittedWork.insert(std::make_pair(used, &worker));
	ScopedFuncCall popSubmitted([this, used]() { this->submittedWork.erase(used); });
	pendingRequests.insert(std::make_pair(used, "mining.submit"));
	popMsg.Dont();
	popSubmitted.Dont();
	worker.nonces.sent++;
    return used;
}


void StratumState::Request(const stratum::ClientGetVersionRequest &msg) {
    // There are various ways to produce a decent version string...
    // But at the end of the day it's just easier to fully rebuild and be done with it.
    const std::string date(__DATE__);
    const std::string time(__TIME__);
	PushResponse(msg.id, KeyValue("result", "M8M/" + date + " " + time));
}


const char* StratumState::Response(size_t id) const {
	auto prev = pendingRequests.find(id);
	if(prev == pendingRequests.cend()) throw std::exception("Server response not mapping to any request.");
	return prev->second;
}


void StratumState::Response(asizei id, const stratum::MiningAuthorizeResponse &msg) {
	auto w(std::find_if(workers.begin(), workers.end(), [id](const Worker &test) { return test.id == id; }));
	if(w == workers.end()) return; // impossible
	if(w->authorized == as_pending) w->authorized = time(NULL);
	if(msg.authorized == msg.ar_bad) w->authStatus = as_failed;
	else if(msg.authorized == msg.ar_notRequired) w->authStatus = as_notRequired;
	else if(msg.authorized == msg.ar_pass) w->authStatus = as_accepted;
    if(workerAuthCallback) workerAuthCallback(w->name, w->authStatus);
}


void StratumState::Response(asizei id, const stratum::MiningSubmitResponse &msg) {
	ScopedFuncCall erase([id, this]() { submittedWork.erase(id); });
	auto w = submittedWork.find(id);
	if(w != submittedWork.end()) { // always happens... but if not, just consider NOP
		if(msg.accepted) w->second->nonces.accepted++;
		else w->second->nonces.rejected++;
		if(w->second->authorized == as_pending) {
			w->second->authorized = time(NULL);
            w->second->authStatus = msg.accepted? as_inferred : as_failed; // or maybe we were just unlucky. That's a real edge case.
            if(workerAuthCallback) workerAuthCallback(w->second->name, w->second->authStatus);
		}
	    if(shareResponseCallback) shareResponseCallback(id, msg.accepted? ssr_accepted : ssr_rejected);
	}
}


void StratumState::Notify(const stratum::MiningNotify &msg) {
	block = msg;
	dataTimestamp = time(NULL);
}


void StratumState::RequestReplyReceived(asizei id, bool error) {
	ScopedFuncCall clear([this, id]() { pendingRequests.erase(pendingRequests.find(id)); });
	if(error) {
		ScopedFuncCall inc([this]() { this->errorCount++; });
		if(std::string(Response(id)) == "mining.submit") Response(id, stratum::MiningSubmitResponse(false));
	}
}
