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
	ScopedFuncCall release([&add]() { delete[] add.data; });
	pendingRequests.insert(std::make_pair(used, method));
	ScopedFuncCall pullout([used, this] { pendingRequests.erase(used); });
	pending.push(add);

	pullout.Dont();
	release.Dont(); // now it'll be released by this dtor
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
	ScopedFuncCall release([&add]() { delete[] add.data; });
	pending.push(add);
	release.Dont();
}


StratumState::StratumState(const char *presentation, std::pair<PoolInfo::DiffMode, PoolInfo::DiffMultipliers> &diffDesc)
	: nextRequestID(1), difficulty(.0), nameVer(presentation), diffMul(diffDesc.second), diffMode(diffDesc.first), errorCount(0) {
	dataTimestamp = 0;
	size_t used = PushMethod("mining.subscribe", KeyValue("params", "[]", false));
	ScopedFuncCall pop([this]() { this->pending.pop(); });
	pendingRequests.insert(std::make_pair(used, "mining.subscribe"));
	pop.Dont();
}


stratum::WorkDiff StratumState::GetCurrentDiff() const {
    stratum::WorkDiff result;
    if(difficulty > 0.0) {
        result.shareDiff = difficulty * diffMul.stratum;
        switch(diffMode) {
        case::PoolInfo::dm_btc:
            result.target = MakeTargetBits_BTC(result.shareDiff, diffMul.one);
            break;
        case::PoolInfo::dm_neoScrypt: 
            result.target = MakeTargetBits_NeoScrypt(result.shareDiff, diffMul.one);
            break;
        default: throw std::exception("Impossible, forgot to update code for target bits generation maybe!");
        }
    }
    return result;
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
	PushResponse(msg.id, KeyValue("result", nameVer.c_str()));
}


const char* StratumState::Response(size_t id) const {
	auto prev = pendingRequests.find(id);
	if(prev == pendingRequests.cend()) throw std::exception("Server response not mapping to any request.");
	return prev->second;
}


void StratumState::Response(asizei id, const stratum::MiningAuthorizeResponse &msg) {
	auto w(std::find_if(workers.begin(), workers.end(), [id](const Worker &test) { return test.id == id; }));
	if(w == workers.end()) return; // impossible
	if(w->authorized == 0) w->authorized = time(NULL);
	if(msg.authorized == msg.ar_bad) w->authStatus = as_failed;
	else if(msg.authorized == msg.ar_notRequired) w->authStatus = as_notRequired;
	else if(msg.authorized == msg.ar_pass) w->authStatus = as_accepted;
	if(msg.authorized == false) {
		asizei failed = 0;
		for(asizei loop = 0; loop < workers.size(); loop++) {
			if(workers[loop].authorized) {
				AuthStatus s = workers[loop].authStatus;
				if(s == as_failed) failed++;
			}
		}
		if(failed == workers.size() && allWorkersFailedAuthCallback) allWorkersFailedAuthCallback();
	}
}


void StratumState::Response(asizei id, const stratum::MiningSubmitResponse &msg) {
	ScopedFuncCall erase([id, this]() { submittedWork.erase(id); });
	auto w = submittedWork.find(id);
	if(w != submittedWork.end()) { // always happens... but if not, just consider NOP
		if(msg.accepted) w->second->nonces.accepted++;
		else w->second->nonces.rejected++;
		if(w->second->authorized == 0) {
			w->second->authorized = time(NULL);
			w->second->authStatus = as_inferred;
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


std::array<aulong, 4> StratumState::MakeTargetBits_BTC(adouble diff, adouble diffOneMul) {
	std::array<aulong, 4> target;
	/*
	Ok, there's this constant, "truediffone" which is specified as a 256-bit value
	0x00000000FFFF0000000000000000000000000000000000000000000000000000
	              |------------------- 52 zeros --------------------|
	So it's basically aushort(0xFFFF) << (52 * 4)
	Or: 65535 * ... 2^208?
	Legacy miners have those values set up, so they can go use double-float division to effectively
	expand the bit representation and select the bits they care. By using multiple passes, they pull
	out successive ranges of reductions. They use the following constants:
	truediffone = 0x00000000FFFF0000000000000000000000000000000000000000000000000000
	bits192     = 0x0000000000000001000000000000000000000000000000000000000000000000
	bits128     = 0x0000000000000000000000000000000100000000000000000000000000000000
	bits64      = 0x0000000000000000000000000000000000000000000000010000000000000000
	Because all those integers have a reduced range, they can be accurately represented by a double.
	See diffCalc.html for a large-integer testing framework. */
	const adouble BITS_192 = 6277101735386680763835789423207666416102355444464034512896.0;
	const adouble BITS_128 = 340282366920938463463374607431768211456.0;
	const adouble BITS_64 = 18446744073709551616.0;

	if(diff == 0.0) diff = 1.0;
	adouble big = (diffOneMul * btc::TRUE_DIFF_ONE) / diff;
	aulong toString;
	const adouble k[4] = { BITS_192, BITS_128, BITS_64, 1 };
	for(asizei loop = 0; loop < 4; loop++) {
		adouble partial = big / k[loop];
		toString = aulong(partial);
		target[4 - loop - 1] = HTOLE(toString);
		partial = toString * k[loop];
		big -= partial;
	}	
	return target;
}


std::array<aulong, 4> StratumState::MakeTargetBits_NeoScrypt(adouble diff, adouble diffOneMul) {
	diff /=	double(1ull << 16);
	auint div = 6;
	while(div <= 6 && diff > 1.0) {
		diff /= double(1ull << 32);
		div--;
	}
	const aulong slice = aulong(double(0xFFFF0000) / diff);
	std::array<aulong, 4> ret;
	bool allSet = slice == 0 && div == 6;
	memset(ret.data(), allSet? 0xFF : 0x00, sizeof(ret));
	if(!allSet) {
		auint *dwords = reinterpret_cast<auint*>(ret.data());
		dwords[div] = auint(slice);
		dwords[div + 1] = auint(slice >> 32);
	}
	return ret;
}
