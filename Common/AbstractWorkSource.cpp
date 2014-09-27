/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "AbstractWorkSource.h"


#if defined(M8M_STRATUM_DUMPTRAFFIC)
static std::ofstream stratumDump;
#endif

AbstractWorkSource::Event AbstractWorkSource::Refresh(bool canRead, bool canWrite) {
    // As a start, dispatch my data to the server. It is required to do this first as we need to
    // initiate connection with a mining.subscribe. Each tick, we send as many bytes as we can until
    // write buffer is exhausted.
	const time_t PREV_WORK_STAMP(stratum.Working());
    if(stratum.pending.size() && canWrite) {
        asizei count = 1;
        while(count && stratum.pending.size()) {
			StratumState::Blob &msg(stratum.pending.front());
            count = Send(msg.data + msg.sent, msg.total - msg.sent);
            msg.sent += count;
            if(msg.sent == msg.total) {
#if defined(M8M_STRATUM_DUMPTRAFFIC)
				stratumDump<<">>sent to server:"<<std::endl;
				for(asizei i = 0; i < msg.total; i++) stratumDump<<msg.data[i];
				stratumDump<<std::endl;
#endif
				stratum.pending.pop();
			}
		}
	}
	if(canRead == false) return e_nop; // sends are still considered nops, as they don't really change the hi-level state
	int received = Receive(recvBuffer.NextBytes(), recvBuffer.Remaining());
	if(!received) return e_nop;

	recvBuffer.used += received;
	if(recvBuffer.Full()) recvBuffer.Grow();
	
	using namespace rapidjson;
	Document object;
	char *pos = recvBuffer.data.get();
	char *lastEndl = nullptr;
	while(pos < recvBuffer.NextBytes()) {
		char *limit = std::find(pos, recvBuffer.NextBytes(), '\n');
		if(limit >= recvBuffer.NextBytes()) pos = limit;
		else { // I process one line at time
#if defined(M8M_STRATUM_DUMPTRAFFIC)
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
	else return e_nop;
	return stratum.Working() != PREV_WORK_STAMP? e_newWork : e_gotRemoteInput;
}


bool AbstractWorkSource::NeedsToSend() const {
	return stratum.pending.size() != 0; // notice on construction this contains the subscription message
}


aulong AbstractWorkSource::GetCoinDiffMul() const { return stratum.coinDiffMul; }


void AbstractWorkSource::GetUserNames(std::vector< std::pair<const char*, StratumState::AuthStatus> > &list) const {
	const asizei prev = list.size();
	list.resize(prev + stratum.GetNumWorkers());
	for(asizei loop = 0; loop < list.size(); loop++) list[loop] = stratum.GetWorkerInfo(loop);
}


AbstractWorkSource::AbstractWorkSource(const char *presentation, const char *poolName, const char *algorithm, aulong coinDiffMul, PoolInfo::MerkleMode mm, const Credentials &v)
	: stratum(presentation, coinDiffMul, mm), algo(algorithm), name(poolName) {
	for(asizei loop = 0; loop < v.size(); loop++) stratum.Authorize(v[loop].first, v[loop].second);
#if defined(M8M_STRATUM_DUMPTRAFFIC)
	stratumDump.open("stratumTraffic.txt");
#endif
	stratum.shareResponseCallback = [this](bool ok) {
		// if(ok) stats.shares.accepted++;
		if(this->shareResponseCallback) this->shareResponseCallback(ok);
	};
}
