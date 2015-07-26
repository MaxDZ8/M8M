/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "AbstractCommand.h"

namespace commands {


class VersionCMD : public AbstractCommand {
	PushInterface* Parse(rapidjson::Document &reply, const rapidjson::Value &input) {
        const char *credits =
            "M8M is free software, obtain a <a href='https://github.com/MaxDZ8/M8M/releases'>new version of the executable</a> or the <a href='https://github.com/MaxDZ8/M8M/'>source code</a> free of charge.<br>"
            "Makes use of the following libraries:"
            "<ul>"
            "<li><a href='https://github.com/miloyip/rapidjson'>rapidjson</a>: Copyright (C) 2015 THL A29 Limited, a Tencent company, and Milo Yip.</li>"
            "<li><a href='http://www.saphir2.com/sphlib/'>sphlib</a>: by \"Projet RNRT SAPHIR\".</li>"
            "</ul>";
        auto &alloc(reply.GetAllocator());
        reply.SetObject();
        reply.AddMember("protocol", 4, alloc);
        rapidjson::Value build(rapidjson::kObjectType);
        build.AddMember("date", __DATE__, alloc); // Note _DATE__ and __TIME__ only work properly if full rebuild is used
        build.AddMember("time", __TIME__, alloc); // otherwise this might get not re-compiled and thus be left in old state
        build.AddMember("msg", rapidjson::StringRef(credits), alloc);
        reply.AddMember("build", build, alloc);
		return nullptr;
	}
public:
	VersionCMD() : AbstractCommand("version") { }
};


}
