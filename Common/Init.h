/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once

#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/encodedstream.h>
#include <rapidjson/error/en.h>
#include "Settings.h"
#include <memory>
#include <functional>
#include <algorithm>
#include "../Common/ScopedFuncCall.h"

Settings* LoadConfig(const wchar_t *filename);
