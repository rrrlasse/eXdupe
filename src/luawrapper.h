// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#include "unicode.h"

#include <stdint.h>
#include <string>

using std::wstring;
bool execute(STRING script2, STRING path2, int type, STRING name2, uint64_t size, STRING ext2, uint32_t attrib, time_t date);