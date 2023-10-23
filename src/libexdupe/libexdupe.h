// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2023: Lasse Mikkel Reinhold

#ifndef DEDUPE_HEADER
#define DEDUPE_HEADER

#define DUP_BLOCK 'D'

#include <string.h>
#include <stdint.h>

uint64_t dup_memory(uint64_t bits);
int dup_init (size_t large_block, size_t small_block, uint64_t memory_usage, int max_threadcount, void *memory, int compression_level);

size_t dup_compress(const void *src, unsigned char *dst, size_t size, bool flush);
int dup_decompress(const unsigned char *src, unsigned char *dst, size_t *length, uint64_t *payload);
int dup_decompress_simulate(const unsigned char *src, size_t *length, uint64_t *payload);
bool dup_busy(void);

size_t dup_size_compressed(const unsigned char *src);
size_t dup_size_decompressed(const unsigned char *src);

void dup_counters_reset(void);
uint64_t dup_counter_payload(void);
uint64_t dup_counter_compressed(void);

void dup_add(bool add);
size_t dup_table_condense(void);
int dup_table_expand(size_t len);
void dup_deinit(void);

void reset_profiling(void);
void print_profiling(void);

#endif

