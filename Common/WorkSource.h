/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractWorkSource.h"
#include "../Common/PoolInfo.h"
#include "../Common/AREN/ScopedFuncCall.h"
#include "../Common/Stratum/parsing.h"
#include "../Common/BTC/Funcs.h"
#include "Network.h"
#include <functional>

/*! A WorkSource is a persistent object inferred from configuration file. Each eligible pool entry  (element in the "pools" array) will get represented from
one of those objects. Period. Those objects are always there.

Whatever they're also working is another problem. Some other component will take care of building those and providing them with a TCP connection to use. */
class WorkSource : public AbstractWorkSource {
public:
	WorkSource(const std::string &name, const AlgoInfo &algoParams, std::pair<PoolInfo::DiffMode, PoolInfo::DiffMultipliers> &diff, PoolInfo::MerkleMode mm)
        : AbstractWorkSource(name.c_str(), algoParams, diff, mm), errorCallback(DefaultErrorCallback(false)) {
    }

    /*! Before a connection can be even used, some credentials must be supplied. Those are pulled from the config file in some way and re-sent to the server
    every time a new ready-to-go connection is supplied. */
    void AddCredentials(std::string username, std::string password) {
        credentials.push_back(std::move(std::pair<std::string, std::string>(std::move(username), std::move(password))));
	}

    //! After a TCP connection has been estabilished and socket is "ready to go", call this to generate internal state and begin sending data.
    //! \note Caller keeps ownership of this pointer. This object does nothing (in lifetime terms).
    //! \returns false if no credentials has been provided.
    bool Use(NetworkInterface::ConnectedSocketInterface *remote);

    //! This call instead destroys the internal stratum state and gives up the previous socket. From now on, the pool is "inactive" until next Use()
    void Disconnected();

	//! This function gets called by MangleError.
	std::function<void(const WorkSource &pool, asizei index, int errorCode, const std::string &message)> errorCallback;

	static std::function<void(const WorkSource &pool, asizei index, int errorCode, const std::string &message)> DefaultErrorCallback(bool silent) {
		if(silent) return [](const WorkSource &pool, asizei i, int errcode, const std::string& message) { };
		return [](const WorkSource &pool, asizei i, int errcode, const std::string& message) {
			std::string err("Stratum message [");
			err += std::to_string(i) + "] generated error response by server (code " + std::to_string(errcode) + ").";
			throw err;
		};
	}

    const NetworkInterface::ConnectedSocketInterface* GetConnection() const { return pipe; }
    void Shutdown() { Disconnected(); /* not really */ }

private:
	NetworkInterface::ConnectedSocketInterface *pipe = nullptr; //! this is supposed to be const (in the sense you don't mess up with it), owned by some other code.
    std::vector< std::pair<std::string, std::string> > credentials;

	template<typename Parser>
	void MangleResult(bool &processed, const char *originally, size_t id, const rapidjson::Value &object, Parser &parser) {
		if(processed) return;
		if(parser.name != originally) return;
		std::unique_ptr<Parser::Product> dispatch(parser.Mangle(object));
		//if(dispatch.get() == nullptr) return; // impossible, would have thrown
		stratum->Response(id, *dispatch.get());
		processed = true;
	}

	template<typename Parser>
	void MangleRequest(bool &processed, const char *methodName, const string &id, const rapidjson::Value &paramsArray, Parser &parser) {
		if(processed) return;
		if(parser.name != methodName) return;
		std::unique_ptr<Parser::Product> dispatch(parser.Mangle(id, paramsArray));
		//if(dispatch.get() == nullptr) return; // impossible, would have thrown
		stratum->Request(*dispatch.get());
		processed = true;
	}

	template<typename Parser>
	void MangleNotification(bool &processed, const char *methodName, const rapidjson::Value &paramsArray, Parser &parser) {
		if(processed) return;
		if(parser.name != methodName) return;
		std::unique_ptr<Parser::Product> dispatch(parser.Mangle(paramsArray));
		//if(dispatch.get() == nullptr) return; // impossible, would have thrown
		stratum->Notify(*dispatch.get());
		processed = true;
	}

protected:
	void MangleReplyFromServer(size_t id, const rapidjson::Value &result, const rapidjson::Value &error);
	void MangleMessageFromServer(const std::string &idstr, const char *signature, const rapidjson::Value &notification);
	std::pair<bool, asizei> Send(const abyte *data, const asizei count);
	std::pair<bool, asizei> Receive(abyte *storage, asizei rem);
	void GetCredentials(std::vector< std::pair<const char*, StratumState::AuthStatus> > &list) const;
};
