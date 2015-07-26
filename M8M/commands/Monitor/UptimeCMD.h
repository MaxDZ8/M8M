/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../AbstractCommand.h"
#include <memory>

namespace commands {
namespace monitor {

class UptimeCMD : public AbstractCommand {
public:
    enum StartTime {
        st_program,
        st_hashing,
        st_firstNonce
    };

    class StartTimeProvider {
    public:
        virtual ~StartTimeProvider() { }
        /*! Return the number of seconds elapsed since epoch. If zero is returned, the specific event didn't happen yet.
        Of course st_program always returns nonzero. */
        virtual std::chrono::seconds GetStartTime(StartTime st) const = 0;
    };

    UptimeCMD(const StartTimeProvider &src) : timings(src), AbstractCommand("uptime") { }


private:
    const StartTimeProvider &timings;

    PushInterface* Parse(rapidjson::Document &build, const rapidjson::Value &input) {
        build.SetObject();
        StartTime key[] = { st_program, st_hashing, st_firstNonce };
        const char *keyName[] = { "program", "hashing", "nonce", nullptr };
        for(asizei loop = 0; keyName[loop]; loop++) {
            auto value = timings.GetStartTime(key[loop]).count();
            if(value) build.AddMember(rapidjson::StringRef(keyName[loop]), value, build.GetAllocator());
        }
        return nullptr;
    }
};


}
}
