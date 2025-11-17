// SPDX-License-Identifier: MIT
//
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2025: Lasse Mikkel Reinhold

#pragma once

#include "unicode.h"
#include "utilities.hpp"

#include <stdint.h>
#include <string>

using std::wstring;
bool execute(STRING script2, STRING path2, int type, STRING name2, uint64_t size, STRING ext2, uint32_t attrib, time_ms_t date, bool top_level);