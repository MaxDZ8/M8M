/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "AbstractWorkSource.h"


#if STRATUM_DUMPTRAFFIC
#include <fstream>
std::ofstream stratumDump("stratumTraffic.txt");
#endif

AbstractWorkSource::Events AbstractWorkSource::Refresh(bool canRead, bool canWrite) {
    // This is public and due to TCP async connect nature, we cannot assume we're ready to go so shortcircuit if still waiting.
    if(!stratum) return Events();

    // As a start, dispatch my data to the server. It is required to do this first as we need to
    // initiate connection with a mining.subscribe. Each tick, we send as many bytes as we can until
    // write buffer is exhausted.
	const time_t PREV_WORK_STAMP(stratum->LastNotifyTimestamp());
    if(canWrite && !SendChunk()) {
        Events ohno;
        ohno.connFailed = true;
        return ohno;
    }
    Events ret;
	if(canRead == false) return ret; // sends are still considered nops, as they don't really change the hi-level state
    auto received(Receive(recvBuffer.NextBytes(), recvBuffer.Remaining()));
    if(received.first == false) {
        ret.connFailed = true;
        return ret;
    }
    else if(received.second == 0) return ret;
    ret.bytesReceived += received.second;
	recvBuffer.used += received.second;
	if(recvBuffer.Full()) recvBuffer.Grow();
	
	using namespace rapidjson;
	Document object;
	char *pos = recvBuffer.data.get();
	char *lastEndl = nullptr;
    const auto prevDiff(GetCurrentDiff());
    const auto prevJob(stratum->GetCurrentJob());
	while(pos < recvBuffer.NextBytes()) {
		char *limit = std::find(pos, recvBuffer.NextBytes(), '\n');
		if(limit >= recvBuffer.NextBytes()) pos = limit;
		else {
            ProcessLine(object, pos, limit - pos);
            pos = limit;
            lastEndl = pos++;

        }
	}
	if(lastEndl) { // at least a line has been mangled
		const char *src = lastEndl + 1;
		char *dst = recvBuffer.data.get();
		for(; src < recvBuffer.NextBytes(); src++, dst++) *dst = *src;
		recvBuffer.used = src - (lastEndl + 1);
#if _DEBUG
		for(; dst < recvBuffer.data.get() + recvBuffer.allocated; dst++) *dst = 0;
#endif
	}
	else return ret;
    const auto nowDiff(GetCurrentDiff());
    const auto nowJob(stratum->GetCurrentJob());
    auto different = [](const stratum::MiningNotify &one, const stratum::MiningNotify &two) {
        if(one.job != two.job || one.ntime != two.ntime || one.clear != two.clear) return true; // most likely
        if(one.prevHash != two.prevHash) return true; // fairly likely
        // blockVer is mostly constant,
        // nbits is... ?
        if(one.merkles.size() != two.merkles.size()) return true; // happens quite often
        if(one.coinBaseOne.size() != two.coinBaseOne.size() || one.coinBaseTwo.size() != two.coinBaseTwo.size()) return true; // not likely, if at all possible
        bool diff = false;
        for(asizei i = 0; i < one.merkles.size(); i++) diff |= one.merkles[i].hash != two.merkles[i].hash;
        for(asizei i = 0; i < one.coinBaseOne.size(); i++) diff |= one.coinBaseOne[i] != two.coinBaseOne[i];
        for(asizei i = 0; i < one.coinBaseTwo.size(); i++) diff |= one.coinBaseTwo[i] != two.coinBaseTwo[i];
        return diff;
    };
    ret.diffChanged = nowDiff != prevDiff;
    ret.newWork = different(prevJob, nowJob) && GetCurrentDiff().shareDiff != .0; // new work is to be delayed as long as diff is 0
    if(prevDiff.shareDiff == .0 && nowDiff.shareDiff != .0) {
        // When this happens and we already have a job of any sort we can finally flush the new work to the outer code
        if(nowJob.job.size()) ret.newWork = true;
    }
    return ret;
}


stratum::AbstractWorkFactory* AbstractWorkSource::GenWork() const {
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

	// First step is to generate the coin base, which is a function of the nonce and the block.
	const auint nonce2 = 0; // not really used anymore. We always generate starting from (0,0) now.
    const auto diff(GetCurrentDiff());
    const auto work(stratum->GetCurrentJob());
    const auto subscription(stratum->GetSubscription());
	if(sizeof(nonce2) != subscription.extraNonceTwoSZ)  throw std::exception("nonce2 size mismatch");
	if(diff.shareDiff <= 0.0) throw std::exception("GenWork must be called only when new work is signaled as available!");

    auto btcLikeMerkle = [](std::array<aubyte, 32> &imerkle, const std::vector<aubyte> &coinbase) {
        btc::SHA256Based(imerkle, coinbase.data(), coinbase.size());
    };
    auto singleSHA256Merkle = [](std::array<aubyte, 32> &imerkle, const std::vector<aubyte> &coinbase) {
		using hashing::BTCSHA256;
		BTCSHA256 hasher(BTCSHA256(coinbase.data(), coinbase.size()));
		hasher.GetHash(imerkle);
    };
    stratum::AbstractWorkFactory::CBHashFunc merkleFunc;
    switch(merkleMode) {
    case PoolInfo::dm_btc: merkleFunc = btcLikeMerkle; break;
    case PoolInfo::dm_neoScrypt: merkleFunc = singleSHA256Merkle; break;
    default:
        throw std::exception("Unrecognized merkle mode - code out of sync");
    }
    auto ret(std::make_unique<BuildingWorkFactory>(work.clear, work.ntime, merkleFunc, work.job));
	const asizei takes = work.coinBaseOne.size() + subscription.extraNonceOne.size() + subscription.extraNonceTwoSZ + work.coinBaseTwo.size();
    std::vector<aubyte> coinbase(takes);
    asizei off = 0;
	memcpy_s(coinbase.data() + off, takes - off, work.coinBaseOne.data(), work.coinBaseOne.size());                     off += work.coinBaseOne.size();
	memcpy_s(coinbase.data() + off, takes - off, subscription.extraNonceOne.data(), subscription.extraNonceOne.size());	off += subscription.extraNonceOne.size();
	const asizei nonce2Off = off;
	memcpy_s(coinbase.data() + off, takes - off, &nonce2, sizeof(nonce2));												off += sizeof(nonce2);
	memcpy_s(coinbase.data() + off, takes - off, work.coinBaseTwo.data(), work.coinBaseTwo.size());
    ret->SetCoinbase(coinbase, nonce2Off);
    ret->SetMerkles(work.merkles, 4 + work.prevHash.size());

	const auint clearNonce = 0;
	const size_t sz = sizeof(work.blockVer) + sizeof(work.prevHash) + 32 +
		              sizeof(work.ntime) + sizeof(work.nbits) + sizeof(clearNonce) + 
					  sizeof(workPadding);
	std::array<aubyte, 128> newHeader;
	if(sz != newHeader.size()) throw std::exception("Incoherent source, block header size != 128B");
	DestinationStream dst(newHeader.data(), sizeof(newHeader));
	dst<<work.blockVer<<work.prevHash;
	for(size_t loop = 0; loop < 32; loop++) dst<<aubyte(0);
	dst<<work.ntime<<work.nbits<<clearNonce<<workPadding;
    ret->SetBlankHeader(newHeader, !algo.bigEndian, algo.diffNumerator);
    return ret.release();
}


stratum::WorkDiff AbstractWorkSource::GetCurrentDiff() const {
    stratum::WorkDiff result;
    if(stratum) {
        auto difficulty(stratum->GetCurrentDiff());
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
    }
    return result;
}


bool AbstractWorkSource::NeedsToSend() const {
	return stratum && stratum->pending.size() != 0; // notice on construction this contains the subscription message
}


void AbstractWorkSource::GetUserNames(std::vector< std::pair<const char*, StratumState::AuthStatus> > &list) const {
    if(stratum) {
	    const asizei prev = list.size();
	    list.resize(prev + stratum->GetNumWorkers());
	    for(asizei loop = 0; loop < list.size(); loop++) list[loop] = stratum->GetWorkerInfo(loop);
    }
    else GetCredentials(list);
}


AbstractWorkSource::AbstractWorkSource(const char *poolName, const CanonicalInfo &algorithm, std::pair<PoolInfo::DiffMode, PoolInfo::DiffMultipliers> diffDesc, PoolInfo::MerkleMode mm)
	: diffMode(diffDesc.first), diffMul(diffDesc.second), algo(algorithm), name(poolName), merkleMode(mm) {
}


void AbstractWorkSource::NewStratum(const Credentials &users) {
    stratum.reset(new StratumState);
    SetStratumCallbacks();
    for(const auto &auth : users) stratum->Authorize(auth.first, auth.second);
}


void AbstractWorkSource::ClearStratum() {
    stratum.reset();
    memset(recvBuffer.data.get(), 0, recvBuffer.allocated); // be extra special sure
}


std::array<aulong, 4> AbstractWorkSource::MakeTargetBits_BTC(adouble diff, adouble diffOneMul) {
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


std::array<aulong, 4> AbstractWorkSource::MakeTargetBits_NeoScrypt(adouble diff, adouble diffOneMul) {
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


bool AbstractWorkSource::SendChunk() {
    if(stratum->pending.empty()) return true;
    asizei count = 1;
    while(count && stratum->pending.size()) {
		StratumState::Blob &msg(stratum->pending.front());
        auto sent(Send(msg.data.data() + msg.sent, msg.total - msg.sent));
        if(sent.first == false) return false;
        count = sent.second;
        msg.sent += count;
        if(msg.sent == msg.total) {
#if STRATUM_DUMPTRAFFIC
			stratumDump<<">>sent to server:"<<std::endl;
			for(asizei i = 0; i < msg.total; i++) stratumDump<<msg.data[i];
			stratumDump<<std::endl;
#endif
			stratum->pending.pop();
		}
	}
    return true;
}


void AbstractWorkSource::ProcessLine(rapidjson::Document &json, char *pos, asizei len) {
#if STRATUM_DUMPTRAFFIC
	stratumDump<<">>from server:";
	for(asizei i = 0; i < len; i++) stratumDump<<pos[i];
	stratumDump<<std::endl;
#endif
	ScopedFuncCall restoreChar([pos, len]() { pos[len] = '\n'; }); // not really necessary but I like the idea
	pos[len] = 0;
	json.ParseInsitu(pos);
    using namespace rapidjson;
	const Value::ConstMemberIterator &id(json.FindMember("id"));
	const Value::ConstMemberIterator &method(json.FindMember("method"));
	// There was a time in which stratum had "notifications" and "requests". They were the same thing basically but requests had an id 
	// to be used for confirmations. Besides some idiot wanted to use 0 as an ID, P2Pool servers always attach an ID even to notifications,
	// which is a less brain-damaged thing but still mandated some changes here.
	std::string idstr;
	aulong idvalue = 0;
	if(id != json.MemberEnd()) {
		switch(id->value.GetType()) {
		case kNumberType: {
			if(id->value.IsUint()) idvalue = id->value.GetUint();
			else if(id->value.IsInt()) idvalue = id->value.GetInt();
			else if(id->value.IsUint64()) idvalue = id->value.GetUint64();
			else if(id->value.IsInt64()) idvalue = id->value.GetInt64();
			idstr = std::to_string(idvalue);
			break;
		}
		case kStringType: 
			idstr.assign(id->value.GetString(), id->value.GetStringLength());
			for(size_t check = 0; check < idstr.length(); check++) {
				char c = idstr[check];
				if(c < '0' || c > '9') throw std::exception("All my ids are naturals>0, this should be a natural>0 number!");
			}
			idvalue = strtoul(idstr.c_str(), NULL, 10);
			break;
		}
	}
	/* If you read the minimalistic documentation of stratum you get the idea you can trust on some things.
	No, you don't. The only real thing you can do is figure out if something is a request from the server or a reply. */
	if(method != json.MemberEnd() && method->value.IsString()) {
		if(method->value.GetString()) MangleMessageFromServer(idstr, method->value.GetString(), json["params"]);
	}
	else { // I consider it a reply. Then it has .result or .error... MAYBE. P2Pool for example sends .result=.error=null as AUTH replies to say it doesn't care about who's logging in!
		MangleReplyFromServer(idvalue, json["result"], json["error"]);
	}
}