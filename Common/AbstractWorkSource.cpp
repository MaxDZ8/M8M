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
    // As a start, dispatch my data to the server. It is required to do this first as we need to
    // initiate connection with a mining.subscribe. Each tick, we send as many bytes as we can until
    // write buffer is exhausted.
	const time_t PREV_WORK_STAMP(stratum.LastNotifyTimestamp());
    if(stratum.pending.size() && canWrite) {
        asizei count = 1;
        while(count && stratum.pending.size()) {
			StratumState::Blob &msg(stratum.pending.front());
            count = Send(msg.data + msg.sent, msg.total - msg.sent);
            msg.sent += count;
            if(msg.sent == msg.total) {
#if STRATUM_DUMPTRAFFIC
				stratumDump<<">>sent to server:"<<std::endl;
				for(asizei i = 0; i < msg.total; i++) stratumDump<<msg.data[i];
				stratumDump<<std::endl;
#endif
				stratum.pending.pop();
			}
		}
	}
    Events ret;
	if(canRead == false) return ret; // sends are still considered nops, as they don't really change the hi-level state
	asizei received = Receive(recvBuffer.NextBytes(), recvBuffer.Remaining());
	if(!received) return ret;
    ret.bytesReceived += received;

	recvBuffer.used += received;
	if(recvBuffer.Full()) recvBuffer.Grow();
	
	using namespace rapidjson;
	Document object;
	char *pos = recvBuffer.data.get();
	char *lastEndl = nullptr;
    const auto prevDiff(stratum.GetCurrentDiff());
    const auto prevJob(stratum.GetCurrentJob());
	while(pos < recvBuffer.NextBytes()) {
		char *limit = std::find(pos, recvBuffer.NextBytes(), '\n');
		if(limit >= recvBuffer.NextBytes()) pos = limit;
		else { // I process one line at time
#if STRATUM_DUMPTRAFFIC
			stratumDump<<">>from server:";
			for(asizei i = 0; pos + i < limit; i++) stratumDump<<pos[i];
			stratumDump<<std::endl;
#endif
			lastEndl = limit;
			ScopedFuncCall restoreChar([limit]() { *limit = '\n'; }); // not really necessary but I like the idea
			*limit = 0;
			object.ParseInsitu(pos);
			const Value::ConstMemberIterator &id(object.FindMember("id"));
			const Value::ConstMemberIterator &method(object.FindMember("method"));
			// There was a time in which stratum had "notifications" and "requests". They were the same thing basically but requests had an id 
			// to be used for confirmations. Besides some idiot wanted to use 0 as an ID, P2Pool servers always attach an ID even to notifications,
			// which is a less brain-damaged thing but still mandated some changes here.
			std::string idstr;
			aulong idvalue = 0;
			if(id != object.MemberEnd()) {
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
			if(method != object.MemberEnd() && method->value.IsString()) {
				if(method->value.GetString()) MangleMessageFromServer(idstr, method->value.GetString(), object["params"]);
			}
			else { // I consider it a reply. Then it has .result or .error... MAYBE. P2Pool for example sends .result=.error=null as AUTH replies to say it doesn't care about who's logging in!
				MangleReplyFromServer(idvalue, object["result"], object["error"]);
			}
			pos = lastEndl + 1;
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
    const auto nowDiff(stratum.GetCurrentDiff());
    const auto nowJob(stratum.GetCurrentJob());
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
    ret.newWork = different(prevJob, nowJob);
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
    const auto diff(stratum.GetCurrentDiff());
    const auto work(stratum.GetCurrentJob());
    const auto subscription(stratum.GetSubscription());
	if(sizeof(nonce2) != subscription.extraNonceTwoSZ)  throw std::exception("nonce2 size mismatch");
	if(diff.shareDiff <= 0.0) throw std::exception("I need to check out this to work with diff 0");

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
    ret->SetBlankHeader(newHeader);
    return ret.release();
}


bool AbstractWorkSource::NeedsToSend() const {
	return stratum.pending.size() != 0; // notice on construction this contains the subscription message
}


PoolInfo::DiffMultipliers AbstractWorkSource::GetDiffMultipliers() const { return stratum.diffMul; }


void AbstractWorkSource::GetUserNames(std::vector< std::pair<const char*, StratumState::AuthStatus> > &list) const {
	const asizei prev = list.size();
	list.resize(prev + stratum.GetNumWorkers());
	for(asizei loop = 0; loop < list.size(); loop++) list[loop] = stratum.GetWorkerInfo(loop);
}


AbstractWorkSource::AbstractWorkSource(const char *presentation, const char *poolName, const char *algorithm, const PoolInfo::DiffMultipliers &diffMul, PoolInfo::MerkleMode mm, const Credentials &v)
	: stratum(presentation, diffMul), algo(algorithm), name(poolName), merkleMode(mm) {
	for(asizei loop = 0; loop < v.size(); loop++) stratum.Authorize(v[loop].first, v[loop].second);
	stratum.shareResponseCallback = [this](asizei index, StratumShareResponse status) {
		// if(ok) stats.shares.accepted++;
		if(this->shareResponseCallback) this->shareResponseCallback(*this, index, status);
	};
}
