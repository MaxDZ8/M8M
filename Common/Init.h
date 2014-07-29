/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once

#include <gflags/gflags.h>
#include <fstream>
#include <json/reader.h>
#include "Settings.h"
#include <memory>
#include <functional>
#include <algorithm>


DECLARE_string(config);

Settings* LoadConfig();
