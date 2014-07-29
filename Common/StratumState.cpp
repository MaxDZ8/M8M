/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "StratumState.h"


size_t StratumState::PushMethod(const char *method, const string &pairs) {
	size_t used = nextRequestID++;
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


StratumState::StratumState(const char *presentation, aulong diffOneMul, PoolInfo::MerkleMode mm)
	: nextRequestID(1), difficulty(16.0), nameVer(presentation), merkleOffset(0), coinDiffMul(diffOneMul), merkleMode(mm), errorCount(0) {
	dataTimestamp = 0;
	size_t used = PushMethod("mining.subscribe", KeyValue("params", "[]", false));
	ScopedFuncCall pop([this]() { this->pending.pop(); });
	pendingRequests.insert(std::make_pair(used, "mining.subscribe"));
	pop.Dont();
}


stratum::WorkUnit* StratumState::GenWorkUnit(auint nonce2) const {
	// First step is to generate the coin base, which is a function of the nonce and the block.
	if(sizeof(nonce2) != subscription.extraNonceTwoSZ)  throw std::exception("nonce2 size mismatch");
	if(difficulty <= 0.0) throw std::exception("I need to check out this to work with diff 0");
	const asizei takes = block.coinBaseOne.size() + subscription.extraNonceOne.size() + subscription.extraNonceTwoSZ + block.coinBaseTwo.size();
	std::vector<aubyte> coinbase(takes);
	{
		asizei off = 0;
		memcpy_s(coinbase.data() + off, takes - off, block.coinBaseOne.data(), block.coinBaseOne.size());					off += block.coinBaseOne.size();
		memcpy_s(coinbase.data() + off, takes - off, subscription.extraNonceOne.data(), subscription.extraNonceOne.size());	off += subscription.extraNonceOne.size();
		memcpy_s(coinbase.data() + off, takes - off, &nonce2, sizeof(nonce2));												off += sizeof(nonce2);
		memcpy_s(coinbase.data() + off, takes - off, block.coinBaseTwo.data(), block.coinBaseTwo.size());
	}
	// Then we have the merkle root, we left it blank in the header.
	std::array<aubyte, 32> merkleRoot;
	switch(merkleMode) {
	case PoolInfo::mm_SHA256D: btc::SHA256Based(merkleRoot, coinbase.data(), coinbase.size()); break;
	case PoolInfo::mm_singleSHA256: {
		using hashing::BTCSHA256;
		BTCSHA256 hasher(BTCSHA256(coinbase.data(), coinbase.size()));
		hasher.GetHash(merkleRoot);
		break;
	}
	default:
		throw std::exception("Code out of sync. Unknown merkle mode.");
	}
	std::array<aubyte, 64> merkleSHA;
	std::copy(merkleRoot.cbegin(), merkleRoot.cend(), merkleSHA.begin());
	for(asizei loop = 0; loop < block.merkles.size(); loop++) {
		auto &sign(block.merkles[loop].hash);
		std::copy(sign.cbegin(), sign.cend(), merkleSHA.begin() + 32);
		btc::SHA256Based(DestinationStream(merkleRoot.data(), sizeof(merkleRoot)), merkleSHA);
		std::copy(merkleRoot.cbegin(), merkleRoot.cend(), merkleSHA.begin());
	}
	btc::FlipIntegerBytes<8>(merkleRoot.data(), merkleSHA.data());

	std::array<aubyte, 128> signature;
	aubyte *header = signature.data();
	memcpy_s(header, 128, blankHeader.data(), sizeof(blankHeader));
	header += merkleOffset;
	memcpy_s(header, 128 - merkleOffset, merkleRoot.data(), sizeof(merkleRoot));

	// The difficulty parameter here should always be non-zero. In some other cases instead it will be 0, 
	// in that case, produce the difficulty value by the work target string.
	stratum::WorkUnit *ret = new stratum::WorkUnit(
		stratum::WUJobInfo(subscription.extraNonceOne, block.job),
		block.ntime,
		stratum::WUDifficulty(difficulty, btc::MakeTargetBits(difficulty, adouble(coinDiffMul))),
		signature, nonce2);
	return ret;
}


void StratumState::Authorize(const char *user, const char *psw) {
	if(std::find_if(workers.cbegin(), workers.cend(), [user](const Worker &test) { return test.name == user; }) != workers.cend()) return;
	std::string identification("[\"");
	identification += user;
	identification += "\", \"";
	identification += psw;
	identification += "\"]";
	size_t used = PushMethod("mining.authorize", KeyValue("params", identification, false));
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


std::pair<bool, bool> StratumState::CanSendWork(const char *name) const {
	auto match(std::find_if(workers.cbegin(), workers.cend(), [name](const Worker &test) { return test.name == name; }));
	if(match == workers.cend()) return std::make_pair(false, false);
	bool reply = match->authorized != 0;
	return std::make_pair(reply, reply? match->canWork : false);
}


void StratumState::SendWork(auint ntime, auint nonce2, auint nonce) {
	const char *user = workers[0].name.c_str(); //!< \todo quick hack to match, should be tracked by worker!
	auto worker(std::find_if(workers.cbegin(), workers.cend(), [user](const Worker &test) { return test.name == user; }));
	if(worker == workers.cend()) return; //!< \todo maybe I should pop an error here

	const char *sep = "\", \"";
	std::stringstream params;
	params<<std::hex;
	params<<"[\""<<worker->name<<sep<<block.job<<sep
		  <<std::setw(8)<<std::setfill('0')<<nonce2<<sep
		  <<std::setw(8)<<std::setfill('0')<<ntime<<sep
		  <<std::setw(8)<<std::setfill('0')<<HTON(nonce)<<"\"]";
	const std::string message(params.str());
	size_t used = PushMethod("mining.submit", KeyValue("params", message, false));
	ScopedFuncCall popMsg([this]() { this->pending.pop(); });
	pendingRequests.insert(std::make_pair(used, "mining.submit"));
	shares.sent++;
	popMsg.Dont();
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
	if(msg.authorized == false) {
		throw std::exception("Worker failed authorization, check your config file, this is not tolerated!");
	}
	auto w(std::find_if(workers.begin(), workers.end(), [id](const Worker &test) { return test.id == id; }));
	if(w != workers.end()) {
		if(w->authorized == 0) w->authorized = time(NULL);
		w->canWork = true;
	}
	else throw std::exception("authorization response unmatched worker, not supposed to happen");
	// glitch? coincidence? Is that fatal? Most likely just won't happen.
}


void StratumState::Response(asizei id, const stratum::MiningSubmitResponse &msg) {
	if(msg.accepted) shares.accepted++;
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
	const size_t clearNonce = 0;
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
