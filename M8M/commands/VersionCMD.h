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
            "<p><h2>M8M - A minimalistic, hopefully educational cryptocurrency miner</h2><em>Permissively released under MIT license.</em><br>"
            "M8M is free software, obtain a <a href=\"https://github.com/MaxDZ8/M8M/releases\">new version of the executable</a> or the <a href=\"https://github.com/MaxDZ8/M8M/\"source code</a> free of charge.</p>"
            "Makes use of the following libraries:"
            "<ul>"
            "<li><a href='https://github.com/miloyip/rapidjson'>rapidjson</a>: Copyright (C) 2015 THL A29 Limited, a Tencent company, and Milo Yip.</li>"
            "<li><a href='http://www.saphir2.com/sphlib/'>sphlib</a>: by \"Projet RNRT SAPHIR\".</li>"
            "</ul>"
            "<h2>Terms of the MIT License:</h2>"
            "Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the \"Software\"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:<br>"
            "<br>"
            "The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.<br>"
            "<br>"
            "<br>THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.<br>"
            "<h2>SPH license</h2>"
            "Copyright (c) 2007-2011  Projet RNRT SAPHIR<br>"
            "<br>"
            "Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the \"Software\"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:<br>"
            "<br>"
            "<br>The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.</br>"
            "<br>"
            "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF"
            "MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT."
            "IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.";
        auto &alloc(reply.GetAllocator());
        reply.SetObject();
        reply.AddMember("protocol", 4, alloc);
        rapidjson::Value build(rapidjson::kObjectType);
        build.AddMember("date", __DATE__, alloc); // Note _DATE__ and __TIME__ only work properly if full rebuild is used
        build.AddMember("time", __TIME__, alloc); // otherwise this might get not re-compiled and thus be left in old state
        build.AddMember("msg", rapidjson::StringRef(credits), alloc);
        reply.AddMember("build", build, alloc);
        reply.SetUint(3);
		return nullptr;
	}
public:
	VersionCMD() : AbstractCommand("version") { }
};


}
