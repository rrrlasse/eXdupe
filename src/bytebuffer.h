// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#include <cstddef>
#include <stdint.h>

void buffer_add(const unsigned char *src, uint64_t payload, size_t len);
char *buffer_find(uint64_t payload, size_t len);
void buffer_init(size_t mem);
