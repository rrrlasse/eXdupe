// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#include <string>
#include <windows.h>
#include <vector>
#include "../unicode.h"

int shadow(std::vector<STRING> vol);
STRING snap(STRING path);
STRING unsnap(STRING path);

void unshadow(void);
STRING DisplayVolumePaths(__in PWCHAR VolumeName);
STRING snappart(STRING path);
STRING volpart(STRING path);
std::vector<std::pair<STRING, STRING>> get_snaps(void);
