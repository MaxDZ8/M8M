/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "HandShaker.h"

#include <fstream>

namespace ws {


const asizei HandShaker::maxHeaderBytes = 4 * 1024;
const asizei HandShaker::headerIncrementBytes = 512;
const char HandShaker::CR = 13;
const char HandShaker::LF = 10;


void HandShaker::Receive() {
	asizei prev = used;
	while(stream.GotData()) {
		if(used == header.size()) header.resize(header.size() + headerIncrementBytes);
		asizei got = stream.Receive(header.data() + used, header.size() - used);
		used += got;
	}

	asizei headerEnd = prev - (prev? 1 : 0);
	auint nl = 0;
	while(headerEnd + 3 < used && nl != 2) {
		nl = 0;
		for(asizei loop = 0; loop < 2; loop++) {
			if(header[headerEnd++] != CR) break;
			if(header[headerEnd++] != LF) throw std::exception("valid HTTP is always CR,LF");
			nl++;
		}
	}
	if(nl != 2) return;
	
	std::vector<std::string> lines; // I keep it easy!
	asizei line = 0, lineBeg = 0;
	while(line < used - 2) {
		const asizei lineend = line;
		if(header[line++] == CR && header[line++] == LF) {
			std::string add(header.data() + lineBeg, header.data() + lineend);
			if(LWS(header[lineBeg])) lines.back() += add;
			else lines.push_back(std::move(add));
			lineBeg = line;
		}
	}
	
	{ // HTTP GET, Request URI
		if(strncmp(lines[0].c_str(), "GET", 3) || !LWS(lines[0][3])) throw std::exception("First line must be a GET request!");
		line = 3;
		while(lines[0][line] && lines[0][line] != '/') line++;
		if(lines[0][line] == 0) throw std::exception("Not a valid HTTP GET request.");
		line++;
		if(strncmp(lines[0].c_str() + line, resource.c_str(), resource.length())) throw std::string("Bad resource request.");
		line += resource.length();
		//! \todo send 404 not found or similar, see page 23
		while(lines[0][line] && LWS(lines[0][line])) line++;
		if(lines[0][line] == 0 || strncmp(lines[0].c_str() + line, "HTTP", 4)) throw std::exception("GET request not HTTP?");
		line += 4;
		while(lines[0][line] && LWS(lines[0][line])) line++;
		if(lines[0][line] != '/') throw std::exception("Not a valid HTTP GET request.");
		line++;
		while(lines[0][line] && LWS(lines[0][line])) line++;
		if(!lines[0][line]) throw std::exception("Not a valid HTTP GET request.");
		const aint major = atoi(lines[0].c_str() + line);
		while(DIGIT(lines[0][line])) line++;
		if(lines[0][line] != '.') throw std::exception("Not a valid HTTP GET request.");
		line++;
		const aint minor = atoi(lines[0].c_str() + line);
		while(DIGIT(lines[0][line])) line++;
		if(lines[0][line]) throw std::exception("Not a valid HTTP GET request.");
		if(major < 1 || minor < 1) throw std::string("Invalid HTTP version ") + std::to_string(major) + '.' + std::to_string(minor) + ", expected >= 1.1.";
	}

	// (2.) |Host| header not checked. I'm not sure how should I do that because I'm not really a server.
	// (3.) |Upgrade| "websocket" case insensitive
	if(_stricmp(GetHeaderValue(lines, "Upgrade").c_str(), "websocket")) 
		throw std::exception("HTTP request missing valid \"Upgrade\" header.");
	{ // (4.) |Connection| must include "Upgrade" token, case insensitive
		std::vector<std::string> tokens(Split(GetHeaderValue(lines, "Connection"), ','));
		auto match = std::find_if(tokens.cbegin(), tokens.cend(), [](const std::string &test) { return _stricmp("Upgrade", test.c_str()) == 0; });
		if(match == tokens.cend()) 
			throw std::exception("HTTP request missing valid \"Connection\" header.");
	}
	// (5.) |Sec-WebSocket-Key|, very important for handshaking but does not need to be decoded. Must be 16 bytes decoded --> 24 chars encoded.
	key = GetHeaderValue(lines, "Sec-WebSocket-Key");
	if(key.length() != 24) 
		throw std::exception("HTTP request missing valid \"Sec-WebSocket-Key\" header.");
	// (6.) |Sec-WebSocket-Version|
	if(atoi(GetHeaderValue(lines, "Sec-WebSocket-Version").c_str()) != 13) 
		throw std::exception("HTTP request missing valid \"Sec-WebSocket-Version\" header.");
	//! \todo Send 426 upgrade required if this fails, WS protocol see page 23

	// (8.) |Sec-WebSocket-Protocol| this is technically optional for some reason but it's really required IMHO
	{
		std::vector<std::string> protocols(Split(GetHeaderValue(lines, "Sec-WebSocket-Protocol"), ','));
		auto match = std::find(protocols.cbegin(), protocols.cend(), this->protocol);
		if(match == protocols.cend()) 
			throw std::exception("HTTP request missing valid \"Sec-WebSocket-Protocol\" header.");
	}

	// If I am here then I need to build the response!
	const char *webSocketSignature = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	// key = "dGhlIHNhbXBsZSBub25jZQ=="; // testing
	const std::string blob(key + webSocketSignature);
	hashing::SHA160 mangler(reinterpret_cast<const aubyte*>(blob.data()), blob.length());
	hashing::SHA160::Digest hashed;
	mangler.GetHash(hashed);
	const char *lut = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::array<char, 28 + 1> result;
	memset(result.data(), 0, sizeof(result));
	asizei dst = 0;
	for(asizei loop = 0; loop < 18; loop += 3) {
		char a = (hashed[loop + 0] & 0xFC) >> 2;
		char b = ((hashed[loop + 0] & 0x03) << 4) | ((hashed[loop + 1] & 0xF0) >> 4);
		char c = ((hashed[loop + 1] & 0x0F) << 2) | ((hashed[loop + 2] & 0xC0) >> 6);
		char d = hashed[loop + 2] & 0x3F;
		result[dst++] = lut[a];
		result[dst++] = lut[b];
		result[dst++] = lut[c];
		result[dst++] = lut[d];
	}
	result[dst++] = lut[(hashed[18] & 0xFC) >> 2];
	result[dst++] = lut[((hashed[18] & 0x03) << 4) | ((hashed[19] & 0xF0) >> 4)];
	result[dst++] = lut[((hashed[19] & 0x0F) << 2)];
	result[27] = '=';

	std::stringstream resp;
	resp<<"HTTP/1.1 101 Switching Protocols"<<CR<<LF;
	resp<<"Upgrade: websocket"<<CR<<LF;
	resp<<"Connection: Upgrade"<<CR<<LF;
	resp<<"Sec-WebSocket-Accept: "<<result.data()<<CR<<LF;
	resp<<"Sec-WebSocket-Protocol: "<<protocol<<CR<<LF;
	resp<<CR<<LF;
	response = resp.str();
}


std::string HandShaker::GetHeaderValue(const std::vector<std::string> &lines, const char *name) {
	std::string ret; // is "not there" the same thing as "empty"? Sure not for HTTP but for me it'll do for the time being!
	for(asizei loop = 0; loop < lines.size(); loop++) {
		const std::string &test(lines[loop]);
		// Are headers to be searched case-insensitively? I should really re-read the HTTP spec but I'm not all that keen on that!
		asizei next = strlen(name);
		const char *line = test.c_str();
		if(strncmp(line, name, next)) continue;
		while(line[next] && LWS(line[next])) next++;
		if(line[next++] != ':') continue; // perhaps invalid, perhaps just a different header starting with the same name
		while(line[next] && LWS(line[next])) next++;
		ret = line + next;
	}
	return ret; // should that be trimmed?
}


std::vector<std::string> HandShaker::Split(const std::string &list, const char separator) {
	std::vector<std::string> ret;
	asizei beg = 0;
	while(beg < list.length()) {
		asizei end = list.find_first_of(separator, beg);
		if(end == asizei(-1)) end = list.length();
		if(beg < end) {
			while(LWS(list[beg])) ++beg; // trim starting whitespace
			asizei last = end - 1;
			while(end > beg && LWS(list[last])) --last;
			++last; // trim ending whitespace
			ret.push_back(std::string(list.cbegin() + beg, list.cbegin() + last));
			end++;
		}
		beg = end;
	}
	return ret;
}


}
