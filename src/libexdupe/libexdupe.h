#pragma once

#define DUP_HEADER_LEN 17

#include <stdint.h>
#include <string.h>
#include <atomic>

/*
A packet of compressed data can either be a "match" (header starts with "MM") or a "literal" header starts with "TT":

M cccc dddd pppppppp
  c: 32-bit, size of this packet, i.e the value 17 (17 bytes)
  d: 32-bit, number of bytes to copy from p
  p: 64-bit pointer into user payload, always points backwards from current position

T cccc dddd pppppppp <data compressed with some traditional data compression>
 c = 32-bit, size of this packet, including data (i.e. 17 + data)
 d = Size in bytes of the *decompressed* data that follows this header
 p = Offset into the user payload that this package represents
*/


uint64_t dup_memory(uint64_t bits);
int dup_init(size_t large_block, size_t small_block, uint64_t memory_usage,
	     int max_threadcount, void *memory, int compression_level,
	     bool crypto_hash, uint64_t hash_seed, uint64_t basepay);

size_t dup_compress(const void *src, char *dst, size_t size,
		    uint64_t *payloadreturned, bool entropy, char*& retval_start);
int dup_decompress(const char *src, char *dst, size_t *length,
		   uint64_t *payload);
int dup_decompress_simulate(const char *src, size_t *length,
			    uint64_t *payload);

size_t dup_size_compressed(const char *src);
size_t dup_size_decompressed(const char *src);

void dup_counters_reset(void);
uint64_t dup_counter_payload(void);
uint64_t dup_counter_compressed(void);
size_t dup_compress_hashtable(char*);
int dup_decompress_hashtable(char* src);
void dup_deinit(void);

void print_fillratio(void);

void reset_profiling(void);
void print_profiling(void);
uint64_t dup_get_flushed(void);
size_t flush_pend(uint64_t *payloadreturned, char*&retval_start);
void print_table();

extern size_t memsize;

extern std::atomic<uint64_t> largehits;
extern std::atomic<uint64_t> smallhits;
extern std::atomic<uint64_t> congested_large;
extern std::atomic<uint64_t> congested_small;
extern std::atomic<uint64_t> stored_as_literals;
extern std::atomic<uint64_t> literals_compressed_size;
extern std::atomic<uint64_t> hashcalls;
extern std::atomic<uint64_t> unhashed;
extern std::atomic<uint64_t> anomalies_large;
extern std::atomic<uint64_t> anomalies_small;
extern std::atomic<uint64_t> skipped;
extern std::atomic<uint64_t> high_entropy;

extern std::atomic<uint64_t> hits1;
extern std::atomic<uint64_t> hits2;
extern std::atomic<uint64_t> hits3;
extern std::atomic<uint64_t> hits4;

extern char* memory_begin;
extern char* memory_table;
extern char* memory_end;