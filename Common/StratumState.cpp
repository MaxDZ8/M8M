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


StratumState::StratumState(const char *presentation, const PoolInfo::DiffMultipliers &muls, PoolInfo::MerkleMode mm)
	: nextRequestID(1), difficulty(16.0), nameVer(presentation), merkleOffset(0), diffMul(muls), merkleMode(mm), errorCount(0), diffMode(PoolInfo::dm_btc) {
	dataTimestamp = 0;
	size_t used = PushMethod("mining.subscribe", KeyValue("params", "[]", false));
	ScopedFuncCall pop([this]() { this->pending.pop(); });
	pendingRequests.insert(std::make_pair(used, "mining.subscribe"));
	pop.Dont();
}


stratum::AbstractWorkUnit* StratumState::GenWorkUnit() const {
	// First step is to generate the coin base, which is a function of the nonce and the block.
	const auint nonce2 = 0; // not really used anymore. We always generate starting from (0,0) now.
	if(sizeof(nonce2) != subscription.extraNonceTwoSZ)  throw std::exception("nonce2 size mismatch");
	if(difficulty <= 0.0) throw std::exception("I need to check out this to work with diff 0");
	
	// The difficulty parameter here should always be non-zero. In some other cases instead it will be 0, 
	// in that case, produce the difficulty value by the work target string. We check it above anyway but worth a note.
	const adouble target = difficulty * diffMul.stratum;
	std::array<aulong, 4> targetBits(diffMode == PoolInfo::dm_btc? btc::MakeTargetBits(target, diffMul.one) : MakeTargetBits_NeoScrypt(target, diffMul.one));
	stratum::WUJobInfo wuJob(subscription.extraNonceOne, block.job);
	stratum::WUDifficulty wuDiff(target, targetBits);
	std::unique_ptr<stratum::AbstractWorkUnit> retwu;
	switch(merkleMode) {
	case PoolInfo::mm_SHA256D: retwu.reset(new stratum::DoubleSHA2WorkUnit(wuJob, block.ntime, wuDiff, blankHeader)); break;
	case PoolInfo::mm_singleSHA256: retwu.reset(new stratum::SingleSHA2WorkUnit(wuJob, block.ntime, wuDiff, blankHeader)); break;
	default:
		throw std::exception("Code out of sync. Unknown merkle mode.");
	}
	const asizei takes = block.coinBaseOne.size() + subscription.extraNonceOne.size() + subscription.extraNonceTwoSZ + block.coinBaseTwo.size();
	retwu->coinbase.binary.resize(takes);
	asizei off = 0;
	memcpy_s(retwu->coinbase.binary.data() + off, takes - off, block.coinBaseOne.data(), block.coinBaseOne.size());						off += block.coinBaseOne.size();
	memcpy_s(retwu->coinbase.binary.data() + off, takes - off, subscription.extraNonceOne.data(), subscription.extraNonceOne.size());	off += subscription.extraNonceOne.size();
	retwu->coinbase.nonceTwoOff = off;
	memcpy_s(retwu->coinbase.binary.data() + off, takes - off, &nonce2, sizeof(nonce2));												off += sizeof(nonce2);
	memcpy_s(retwu->coinbase.binary.data() + off, takes - off, block.coinBaseTwo.data(), block.coinBaseTwo.size());

	retwu->coinbase.merkles.resize(block.merkles.size());
	for(asizei cp = 0; cp < block.merkles.size(); cp++) retwu->coinbase.merkles[cp] = block.merkles[cp];
	retwu->coinbase.merkleOff = merkleOffset;
	retwu->restart = block.clear;
	return retwu.release();
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
	/* Given stratum data, block header can be generated locally.
	, = concatenate
	header = blockVer,prevHash,BLANK_MERKLE,ntime,nbits,BLANK_NONCE,WORK_PADDING
	Legacy miners do that in hex while I do it in binary, so the sizes are going to be cut in half.
	I also use fixed size for the previous hash (appears guaranteed by BTC protocol), ntime and nbits
	(not sure about those) so work is quite easier.
	BLANK_MERKLE is a sequence of 32 int8(0).
	BLANK_NONCE is int32(0)
	With padding, total size is currently going to be (68+12)+48=128 */
	const std::string PADDING_HEX("000000800000000000000000000000000000000000000000000000000000000000000000000000000000000080020000");
	std::array<unsigned __int8, 48> workPadding;
	stratum::parsing::AbstractParser::DecodeHEX(workPadding, PADDING_HEX);
	const auint clearNonce = 0;
	const size_t sz = sizeof(msg.blockVer) + sizeof(msg.prevHash) + 32 +
		              sizeof(msg.ntime) + sizeof(msg.nbits) + sizeof(clearNonce) + 
					  sizeof(workPadding);
	std::array<aubyte, 128> newHeader;
	if(sz != newHeader.size() || newHeader.size() != blankHeader.size()) throw std::exception("Incoherent source, block header size != 128B");
	DestinationStream dst(newHeader.data(), sizeof(newHeader));
	dst<<msg.blockVer<<msg.prevHash;
	merkleOffset = 4 + msg.prevHash.size();
	for(size_t loop = 0; loop < 32; loop++) dst<<aubyte(0);
	dst<<msg.ntime<<msg.nbits<<clearNonce<<workPadding;

	block = msg;
	blankHeader = newHeader;
	dataTimestamp = time(NULL);
}


void StratumState::RequestReplyReceived(asizei id, bool error) {
	ScopedFuncCall clear([this, id]() { pendingRequests.erase(pendingRequests.find(id)); });
	if(error) {
		ScopedFuncCall inc([this]() { this->errorCount++; });
		if(std::string(Response(id)) == "mining.submit") Response(id, stratum::MiningSubmitResponse(false));
	}
}
