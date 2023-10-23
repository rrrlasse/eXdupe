// eXdupe deduplication file archiver and library
//
// Contributers:
//
// 2010 - 2023: Lasse Mikkel Reinhold
//
// eXdupe is now Public Domain (PD): The world's fastest deduplication with the
// worlds least restrictive terms.

#include <cstddef>
#include <stdint.h>

void buffer_add(const unsigned char *src, uint64_t payload, size_t len);
char *buffer_find(uint64_t payload, size_t len);
void buffer_init(size_t mem);
